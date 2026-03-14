#include "cloud_comm.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include "esp_sntp.h"
#include "time.h"
#include "time_manager.h"
#include "command_processor.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CLOUD_COMM";

// WiFi 配置
#define WIFI_SSID "TP-Link_FE2F"    // S24
#define WIFI_PASS "Qxrbtmp2x8gc127460"    //12746088

// MQTT 配置
#define MQTT_BROKER_URI "mqtts://aelecti.top:8883"
#define DEVICE_ID       "Plant_Block_dev01"   // 可改为从 NVS 读取

// 事件组位
#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1

static EventGroupHandle_t s_event_group = NULL;

// MQTT 客户端句柄
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;

// 消息回调
static cloud_comm_msg_cb_t s_msg_cb = NULL;

// 证书（来自 example.c）
extern const char *server_cert;  // 定义在文件末尾

// ------------------ WiFi 事件处理 ------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, trying to reconnect...");
        s_mqtt_connected = false;  // MQTT 也会断开
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

// ------------------ SNTP 时间同步 ------------------
static void time_sync_notification_cb(struct timeval *tv)
{
    time_t now_utc = tv->tv_sec;
    time_t now_cst = now_utc + 8 * 3600; // UTC+8
    struct tm timeinfo_cst;
    localtime_r(&now_cst, &timeinfo_cst);

    time_manager_update_time(
        timeinfo_cst.tm_year + 1900,
        timeinfo_cst.tm_mon + 1,
        timeinfo_cst.tm_mday,
        timeinfo_cst.tm_hour,
        timeinfo_cst.tm_min,
        timeinfo_cst.tm_sec
    );

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo_cst);
    ESP_LOGI(TAG, "Time synchronized (CST): %s", strftime_buf);
}

static void sync_time(void)
{
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "aelecti.top");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    int retry = 0;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= 10) {
        ESP_LOGI(TAG, "Waiting for time sync... (%d/10)", retry);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (retry > 10) {
        ESP_LOGW(TAG, "Time sync timeout, using local time");
    }
}

// ------------------ MQTT 事件处理 ------------------
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            s_mqtt_connected = true;
            s_mqtt_client = event->client;

            // 订阅命令 topic
            char cmd_topic[64];
            snprintf(cmd_topic, sizeof(cmd_topic), "device/%s/cmd", DEVICE_ID);
            esp_mqtt_client_subscribe(event->client, cmd_topic, 1);
            ESP_LOGI(TAG, "Subscribed to %s", cmd_topic);

            // 发送上线通知（遗嘱会在异常断开时自动发送 offline）
            char status_topic[64];
            snprintf(status_topic, sizeof(status_topic), "registry/%s/status", DEVICE_ID);
            esp_mqtt_client_publish(event->client, status_topic, "online", 0, 1, 0);
            ESP_LOGI(TAG, "Published online status");

            xEventGroupSetBits(s_event_group, MQTT_CONNECTED_BIT);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_mqtt_connected = false;
            break;

        case MQTT_EVENT_DATA:
            if (s_msg_cb) {
                s_msg_cb(event->topic, event->topic_len, event->data, event->data_len);
            }

            // 检查是否为命令主题
            char expected_topic[64];
            snprintf(expected_topic, sizeof(expected_topic), "device/%s/cmd", DEVICE_ID);
            if (event->topic_len == strlen(expected_topic) &&
                strncmp(event->topic, expected_topic, event->topic_len) == 0) {
                
                char cmd_buf[256];
                int copy_len = (event->data_len < sizeof(cmd_buf) - 1) ? event->data_len : sizeof(cmd_buf) - 1;
                memcpy(cmd_buf, event->data, copy_len);
                cmd_buf[copy_len] = '\0';
                
                ESP_LOGI(TAG, "Received command: %s", cmd_buf);
                command_processor_process_frame(cmd_buf);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Transport error: %s", strerror(event->error_handle->esp_transport_sock_errno));
                ESP_LOGE(TAG, "TLS error - check: 1.certificate 2.system time 3.firewall");
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused, return code: %d", event->error_handle->connect_return_code);
            }
        break;

        default:
            break;
    }
}

// ------------------ 初始化 WiFi ------------------
static void wifi_init_sta(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 等待 WiFi 连接（超时 30 秒）
    EventBits_t bits = xEventGroupWaitBits(s_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
    }
}

// ------------------ 启动 MQTT ------------------
static void mqtt_app_start(void)
{
    // 遗嘱消息配置：当设备异常断开时，发布 offline 到 registry/{deviceId}/status
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .broker.verification.certificate = server_cert,
        .session.keepalive = 60,
        .credentials.client_id = DEVICE_ID,
        .session.last_will = {
            .topic = "registry/" DEVICE_ID "/status",
            .msg = "offline",
            .msg_len = 7,
            .qos = 1,
            .retain = false
        }
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "will message set to topic: registry/%s/status, payload: offline", DEVICE_ID);
}

// ------------------ 公共接口 ------------------
void cloud_comm_init(void)
{
    // 创建事件组
    s_event_group = xEventGroupCreate();

    // 初始化 NVS（如果尚未初始化）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Cloud communication module initialized");
}

void cloud_comm_start(void)
{
    // 1. 连接 WiFi
    wifi_init_sta();

    // 2. 同步时间
    sync_time();

    // 3. 启动 MQTT
    mqtt_app_start();
}

bool cloud_comm_is_mqtt_connected(void)
{
    return s_mqtt_connected;
}

void cloud_comm_publish(const char *sub_topic, const char *data)
{
    if (!s_mqtt_connected || !s_mqtt_client) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish");
        return;
    }

    char full_topic[128];
    snprintf(full_topic, sizeof(full_topic), "device/%s/%s", DEVICE_ID, sub_topic);
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, full_topic, data, 0, 1, 0);
    ESP_LOGI(TAG, "Published to %s, msg_id=%d", full_topic, msg_id);
}

void cloud_comm_publish_log(const char *format, ...)
{
    if (!s_mqtt_connected || !s_mqtt_client) {
        return; // 未连接时不发送
    }

    // 获取当前时间字符串
    const char *time_str = time_manager_get_time_string(); // 格式如 "2026/02/16 21:30:09 星期一"
    
    char log_buf[512];
    int len = snprintf(log_buf, sizeof(log_buf), "[%s] ", time_str);
    
    va_list args;
    va_start(args, format);
    len += vsnprintf(log_buf + len, sizeof(log_buf) - len, format, args);
    va_end(args);

    if (len < 0 || len >= sizeof(log_buf)) {
        ESP_LOGW(TAG, "Log message too long, truncated");
    }

    cloud_comm_publish("msg", log_buf);
}

void cloud_comm_register_msg_cb(cloud_comm_msg_cb_t callback)
{
    s_msg_cb = callback;
}

// ------------------ 证书 ------------------
const char *server_cert =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIGADCCBOigAwIBAgIQDAYegfp3cU3tclpZFsVKODANBgkqhkiG9w0BAQsFADBu\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMS0wKwYDVQQDEyRFbmNyeXB0aW9uIEV2ZXJ5d2hlcmUg\n"
    "RFYgVExTIENBIC0gRzIwHhcNMjYwMjAzMDAwMDAwWhcNMjYwNTAzMjM1OTU5WjAW\n"
    "MRQwEgYDVQQDEwthZWxlY3RpLnRvcDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCC\n"
    "AQoCggEBAJyoPPQfX7kico1dul+2GDs7vHj7m/D6SOJ7OT4H4tZNTtep6HdKz3GJ\n"
    "6isStzpcGoAYctosFX8w5XdDB3RPfsSHsytfk/ngZ4F1SqgZ8lQCdHkEjXCGOsJw\n"
    "7YnGjDKG4Aiy6CDa2ISRvR2iN5d+kFKwJXSIhu/hzY7VUpXBfg62W1erhZ8peOZp\n"
    "QHPbPmFiegO0dLJxwxf6ML29UGOpqqrEwWq+t7QuAihFrFTY4AFZgmPw8iBpvkFI\n"
    "Vvffj9vm5/uOKYr8rfXGGmcBzj++D08VidhiiVD70HCY8fX3vpq6mc8yKi7MG7aj\n"
    "wUp7kpcxjN55xw3ORXBXZcpuOZWJcK8CAwEAAaOCAvAwggLsMB8GA1UdIwQYMBaA\n"
    "FHjfkZBf7t6s9sV169VMVVPvJEq2MB0GA1UdDgQWBBQAlOusYCsn7JpQkHb/DaxS\n"
    "CsPdKDAnBgNVHREEIDAeggthZWxlY3RpLnRvcIIPd3d3LmFlbGVjdGkudG9wMD4G\n"
    "A1UdIAQ3MDUwMwYGZ4EMAQIBMCkwJwYIKwYBBQUHAgEWG2h0dHA6Ly93d3cuZGln\n"
    "aWNlcnQuY29tL0NQUzAOBgNVHQ8BAf8EBAMCBaAwHQYDVR0lBBYwFAYIKwYBBQUH\n"
    "AwEGCCsGAQUFBwMCMIGABggrBgEFBQcBAQR0MHIwJAYIKwYBBQUHMAGGGGh0dHA6\n"
    "Ly9vY3NwLmRpZ2ljZXJ0LmNvbTBKBggrBgEFBQcwAoY+aHR0cDovL2NhY2VydHMu\n"
    "ZGlnaWNlcnQuY29tL0VuY3J5cHRpb25FdmVyeXdoZXJlRFZUTFNDQS1HMi5jcnQw\n"
    "DAYDVR0TAQH/BAIwADCCAX8GCisGAQQB1nkCBAIEggFvBIIBawFpAHcAlpdkv1VY\n"
    "l633Q4doNwhCd+nwOtX2pPM2bkakPw/KqcYAAAGcJBblFAAABAMASDBGAiEAo/CL\n"
    "WDXY7xO4ygXdKuLU0jW9ek257oHIY7RDTrma8ecCIQDw5eumbOjLSz6+YLkdsssb\n"
    "m1AKu9/HCg4iAyOFsiNwogB2ABaDLavwqSUPD/A6pUX/yL/II9CHS/YEKSf45x8z\n"
    "E/X6AAABnCQW5QIAAAQDAEcwRQIgap8GdCMYOOnYKnxGyLTY4UwrYQ4Id3HL4/sN\n"
    "/UtXthACIQDt+V90q2HHj8/97jsKrGhJ2peTSPAevgPWEG5/eFC8AgB2AGQRxGyk\n"
    "EuyniRyiAi4AvKtPKAfUHjUnq+r+1QPJfc3wAAABnCQW5REAAAQDAEcwRQIhAPrW\n"
    "LasIY5UxtzEqbkv6h9NaR0MXsreXAOP6h77CFU5RAiBhUcyXm0ti8fHRVGeDmZui\n"
    "c8UPkYjYSmUtcSiLmAXG8zANBgkqhkiG9w0BAQsFAAOCAQEAkPVkaeH7KWpgVPBw\n"
    "oie80iFhmsbIhC2asnusEugCU0wflq4l+YseWqjd3UCzBsT5a2JWhuzKGMJZNzMc\n"
    "5+7EG5W3FqGFkKGF97NFeJiNHotTXEX1/NlueBRDoUbUu4PhQu8GLjF20B/q4qgH\n"
    "ikYmsFiRbz0fcxB4PTxD/eK4NFDJFqvpv57GAAwfiht1n8akd4JkIqx+6g8TF0zH\n"
    "ANlIF84r6tuEOaogLWAtyTRVgDXrIDkBaC+bJkNNtILdVgSrySBmfC2w7J+E36kZ\n"
    "5im0kHFELUUapKw0ntDCiubgSJFj3xcjojch1M7IpVmviJCVJtMjHuLFTE1pJGPm\n"
    "XpXQqQ==\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIIEqjCCA5KgAwIBAgIQDeD/te5iy2EQn2CMnO1e0zANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xNzExMjcxMjQ2NDBaFw0yNzExMjcxMjQ2NDBaMG4xCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xLTArBgNVBAMTJEVuY3J5cHRpb24gRXZlcnl3aGVyZSBEViBUTFMgQ0EgLSBH\n"
    "MjCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAO8Uf46i/nr7pkgTDqnE\n"
    "eSIfCFqvPnUq3aF1tMJ5hh9MnO6Lmt5UdHfBGwC9Si+XjK12cjZgxObsL6Rg1njv\n"
    "NhAMJ4JunN0JGGRJGSevbJsA3sc68nbPQzuKp5Jc8vpryp2mts38pSCXorPR+sch\n"
    "QisKA7OSQ1MjcFN0d7tbrceWFNbzgL2csJVQeogOBGSe/KZEIZw6gXLKeFe7mupn\n"
    "NYJROi2iC11+HuF79iAttMc32Cv6UOxixY/3ZV+LzpLnklFq98XORgwkIJL1HuvP\n"
    "ha8yvb+W6JislZJL+HLFtidoxmI7Qm3ZyIV66W533DsGFimFJkz3y0GeHWuSVMbI\n"
    "lfsCAwEAAaOCAU8wggFLMB0GA1UdDgQWBBR435GQX+7erPbFdevVTFVT7yRKtjAf\n"
    "BgNVHSMEGDAWgBROIlQgGJXm427mD/r6uRLtBhePOTAOBgNVHQ8BAf8EBAMCAYYw\n"
    "HQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMBIGA1UdEwEB/wQIMAYBAf8C\n"
    "AQAwNAYIKwYBBQUHAQEEKDAmMCQGCCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdp\n"
    "Y2VydC5jb20wQgYDVR0fBDswOTA3oDWgM4YxaHR0cDovL2NybDMuZGlnaWNlcnQu\n"
    "Y29tL0RpZ2lDZXJ0R2xvYmFsUm9vdEcyLmNybDBMBgNVHSAERTBDMDcGCWCGSAGG\n"
    "/WwBAjAqMCgGCCsGAQUFBwIBFhxodHRwczovL3d3dy5kaWdpY2VydC5jb20vQ1BT\n"
    "MAgGBmeBDAECATANBgkqhkiG9w0BAQsFAAOCAQEAoBs1eCLKakLtVRPFRjBIJ9LJ\n"
    "L0s8ZWum8U8/1TMVkQMBn+CPb5xnCD0GSA6L/V0ZFrMNqBirrr5B241OesECvxIi\n"
    "98bZ90h9+q/X5eMyOD35f8YTaEMpdnQCnawIwiHx06/0BfiTj+b/XQih+mqt3ZXe\n"
    "xNCJqKexdiB2IWGSKcgahPacWkk/BAQFisKIFYEqHzV974S3FAz/8LIfD58xnsEN\n"
    "GfzyIDkH3JrwYZ8caPTf6ZX9M1GrISN8HnWTtdNCH2xEajRa/h9ZBXjUyFKQrGk2\n"
    "n2hcLrfZSbynEC/pSw/ET7H5nWwckjmAJ1l9fcnbqkU/pf6uMQmnfl0JQjJNSg==\n"
    "-----END CERTIFICATE-----\n";