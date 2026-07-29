#include "cloud_comm.h"
#include "tls_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"      // ← 新：替代 esp_sntp.h
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include "esp_smartconfig.h"
#include "time.h"
#include "time_manager.h"
#include "command_processor.h"
#include "event_bus.h"
#include "vofa_output.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CLOUD_COMM";

// 事件组位
#define WIFI_CONNECTED_BIT      BIT0
#define MQTT_CONNECTED_BIT      BIT1
#define SMART_CREDENTIALS_BIT   BIT2
#define WIFI_RETRY_MAX          3
#define SMART_RETRY_MAX         3
static EventGroupHandle_t s_event_group = NULL;

// MQTT 客户端句柄
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;

// 消息回调
static cloud_comm_msg_cb_t s_msg_cb = NULL;

// SmartConfig 完成标志（用于同步 smartconfig_task 和 wifi_init_sta）
static bool s_smartconfig_done = false;

// ------------------ 事件总线回调 ------------------
static void cloud_on_alarm_event(event_type_t type, const void *data, size_t len, void *ctx)
{
   (void)type;
   (void)len;
   (void)ctx;
   const char *msg = (const char *)data;
   if (msg) {
      cloud_comm_publish("alarm", msg);
   }
}

// ------------------ WiFi 事件处理 ------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started");
        /* 不在这里自动连接，由 wifi_init_sta() 统一管理 */
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d", disconn->reason);
        /* 打印可读原因 */
        switch (disconn->reason) {
            case 2:   ESP_LOGW(TAG, "  → AUTH_EXPIRE"); break;
            case 3:   ESP_LOGW(TAG, "  → AUTH_LEAVE"); break;
            case 4:   ESP_LOGW(TAG, "  → ASSOC_EXPIRE"); break;
            case 15:  ESP_LOGW(TAG, "  → 4WAY_HANDSHAKE_TIMEOUT (likely wrong password)"); break;
            case 201: ESP_LOGW(TAG, "  → NO_AP_FOUND"); break;
            case 202: ESP_LOGW(TAG, "  → AUTH_FAIL"); break;
            case 205: ESP_LOGW(TAG, "  → ASSOC_FAIL"); break;
            default:  break;
        }
        s_mqtt_connected = false;
        /* 不自动重连，由 wifi_init_sta() / smartconfig_task 统一管理 */
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

// ------------------ SmartConfig 事件处理（v5.x 新版）------------------
static void smartconfig_event_handler(void *arg, esp_event_base_t base,
                                      int32_t event_id, void *event_data)
{
    if (base != SC_EVENT) return;
    switch (event_id) {
        case SC_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "SmartConfig: Scan done");
            break;
        case SC_EVENT_FOUND_CHANNEL:
            ESP_LOGI(TAG, "SmartConfig: Found channel");
            break;
        case SC_EVENT_GOT_SSID_PSWD: {
            smartconfig_event_got_ssid_pswd_t *evt =
                (smartconfig_event_got_ssid_pswd_t *)event_data;
            ESP_LOGI(TAG, "SmartConfig: Got SSID=%s", evt->ssid);
            /* 显式保存 WiFi 配置（v5.5.1 库不会自动连接，需手动处理） */
            wifi_config_t wifi_cfg = {0};
            memcpy(wifi_cfg.sta.ssid, evt->ssid, sizeof(wifi_cfg.sta.ssid));
            memcpy(wifi_cfg.sta.password, evt->password, sizeof(wifi_cfg.sta.password));
            wifi_cfg.sta.bssid_set = evt->bssid_set;
            if (evt->bssid_set) {
                memcpy(wifi_cfg.sta.bssid, evt->bssid, sizeof(wifi_cfg.sta.bssid));
            }
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
            ESP_LOGI(TAG, "SmartConfig: credentials saved to NVS");
            /* 通知 smartconfig_task：凭据已就绪 */
            xEventGroupSetBits(s_event_group, SMART_CREDENTIALS_BIT);
            break;
        }
        case SC_EVENT_SEND_ACK_DONE:
            ESP_LOGI(TAG, "SmartConfig: ACK sent, provisioning complete");
            s_smartconfig_done = true;
            break;
        default:
            break;
    }
}

// ------------------ SNTP 时间同步（v5.x 新版 API）------------------
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

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("aelecti.top");
    config.sync_cb = time_sync_notification_cb;
    esp_netif_sntp_init(&config);

    int retry = 0;
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(1000)) == ESP_ERR_TIMEOUT
           && ++retry <= 10) {
        ESP_LOGI(TAG, "Waiting for time sync... (%d/10)", retry);
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

            char cmd_topic[64];
            snprintf(cmd_topic, sizeof(cmd_topic), "device/%s/cmd", DEVICE_ID);
            esp_mqtt_client_subscribe(event->client, cmd_topic, 1);
            ESP_LOGI(TAG, "Subscribed to %s", cmd_topic);

            char status_topic[64];
            snprintf(status_topic, sizeof(status_topic), "registry/%s/status", DEVICE_ID);
            esp_mqtt_client_publish(event->client, status_topic, "online", 0, 1, 0);
            ESP_LOGI(TAG, "Published online status");

            xEventGroupSetBits(s_event_group, MQTT_CONNECTED_BIT);
            event_bus_subscribe(EVENT_ALARM, cloud_on_alarm_event, NULL);
            vofa_output_set_cloud_enabled(true);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_mqtt_connected = false;
            vofa_output_set_cloud_enabled(false);
            break;

        case MQTT_EVENT_DATA:
            if (s_msg_cb) {
                s_msg_cb(event->topic, event->topic_len, event->data, event->data_len);
            }

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

// ============ smartconfig_task 替换 ============
static void smartconfig_task(void *arg)
{
   ESP_LOGI(TAG, "Starting SmartConfig (ESPTouch)...");

   esp_event_handler_instance_register(SC_EVENT, ESP_EVENT_ANY_ID,
                                       &smartconfig_event_handler, NULL, NULL);

   esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
   smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

   /* 第一步：等待收到凭据（60s 超时） */
   EventBits_t bits = xEventGroupWaitBits(s_event_group, SMART_CREDENTIALS_BIT,
                                          pdFALSE, pdFALSE, pdMS_TO_TICKS(60000));

   if (bits & SMART_CREDENTIALS_BIT) {
      ESP_LOGI(TAG, "SmartConfig credentials received, stopping sniffer...");
   } else {
      ESP_LOGW(TAG, "SmartConfig: no credentials received in 60s");
   }

   /* 完全停止 SmartConfig，等待嗅探器硬件彻底关闭 */
   esp_smartconfig_stop();
   vTaskDelay(pdMS_TO_TICKS(2000));

   /* 第二步：用新凭据连接，最多重试 SMART_RETRY_MAX 次 */
   if (bits & SMART_CREDENTIALS_BIT) {
      bool connected = false;
      for (int attempt = 0; attempt < SMART_RETRY_MAX; attempt++) {
         ESP_LOGI(TAG, "SmartConfig connect attempt %d/%d", attempt + 1, SMART_RETRY_MAX);

         xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);

         /* 每次重试前确保彻底断开 */
         esp_wifi_disconnect();
         vTaskDelay(pdMS_TO_TICKS(500));

         esp_err_t err = esp_wifi_connect();
         ESP_LOGI(TAG, "esp_wifi_connect() = %s", esp_err_to_name(err));

         bits = xEventGroupWaitBits(s_event_group, WIFI_CONNECTED_BIT,
                                    pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
         if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "SmartConfig: WiFi connected successfully");
            connected = true;
            break;
         }
         ESP_LOGW(TAG, "SmartConfig connect attempt %d failed", attempt + 1);
      }

      if (!connected) {
         ESP_LOGW(TAG, "SmartConfig: all %d connect attempts failed, credentials saved for next boot", SMART_RETRY_MAX);
         /* 凭据已写入 NVS（在 GOT_SSID_PSWD 事件中），
            下次上电时 wifi_init_sta 的 3 次重试会自动尝试 */
      }
   }

   esp_event_handler_instance_unregister(SC_EVENT, ESP_EVENT_ANY_ID,
                                         &smartconfig_event_handler);
   ESP_LOGI(TAG, "SmartConfig task exiting");
   vTaskDelete(NULL);
}

static bool wifi_connect_stored(void)
{
   wifi_config_t wifi_cfg = {0};
   esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
   if (err != ESP_OK || strlen((char*)wifi_cfg.sta.ssid) == 0) {
      ESP_LOGW(TAG, "No stored WiFi credentials");
      return false;
   }
   ESP_LOGI(TAG, "Trying stored SSID: %s", wifi_cfg.sta.ssid);
   /* 清空上次残留的连接标志 */
   xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);
   esp_err_t connect_err = esp_wifi_connect();
   if (connect_err != ESP_OK) {
      /* ESP_ERR_WIFI_CONN (0x300b) = 已在连接中，不算错误，继续等待 */
      if (connect_err != ESP_ERR_WIFI_CONN) {
         ESP_LOGW(TAG, "esp_wifi_connect() failed: %s (0x%x)", esp_err_to_name(connect_err), connect_err);
      }
   }
   EventBits_t bits = xEventGroupWaitBits(s_event_group, WIFI_CONNECTED_BIT,
                                          pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
   return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void wifi_init_sta(void)
{
   esp_netif_create_default_wifi_sta();
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));
   ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                       &wifi_event_handler, NULL, NULL));
   ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                       &wifi_event_handler, NULL, NULL));
   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
   ESP_ERROR_CHECK(esp_wifi_start());

   /* ---- 手动重试循环（最多 3 次）---- */
   for (int attempt = 0; attempt < WIFI_RETRY_MAX; attempt++) {
      ESP_LOGI(TAG, "WiFi attempt %d/%d", attempt + 1, WIFI_RETRY_MAX);

      if (wifi_connect_stored()) {
         ESP_LOGI(TAG, "WiFi connected using stored credentials");
         esp_wifi_set_ps(WIFI_PS_NONE);
         return;
      }

      ESP_LOGW(TAG, "Attempt %d failed, disconnecting for clean retry...", attempt + 1);
      esp_wifi_disconnect();
      vTaskDelay(pdMS_TO_TICKS(2000));  /* 等待断开完成 */
   }

   /* ---- 全部失败 → 启动 SmartConfig ---- */
   ESP_LOGI(TAG, "Stored credentials failed, starting SmartConfig...");
   xTaskCreate(smartconfig_task, "smartconfig", 4096, NULL, 3, NULL);

   /* 等待 SmartConfig 完成（最长 120s） */
   EventBits_t bits = xEventGroupWaitBits(s_event_group, WIFI_CONNECTED_BIT,
                                          pdFALSE, pdFALSE, pdMS_TO_TICKS(120000));
   if (bits & WIFI_CONNECTED_BIT) {
      ESP_LOGI(TAG, "WiFi connected via SmartConfig");
   } else {
      ESP_LOGE(TAG, "SmartConfig timeout, WiFi not connected");
   }
   esp_wifi_set_ps(WIFI_PS_NONE);
}

// ------------------ 启动 MQTT ------------------
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .broker.verification.certificate = server_cert,
        .session.keepalive = 30,
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
    s_event_group = xEventGroupCreate();

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
    wifi_init_sta();
    sync_time();
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
        return;
    }

    char time_str[64];
    time_manager_get_time_string_safe(time_str, sizeof(time_str));
    
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