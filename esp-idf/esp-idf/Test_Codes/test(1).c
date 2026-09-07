#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "mqtt_client.h"

static const char *TAG = "MAIN_APP";

#define WIFI_SSID "TP-Link_FE2F"
#define WIFI_PASS "Qxrbtmp2x8gc127460"
#define MQTT_BROKER_URI "mqtts://aelecti.top:8883"
#define DEVICE_ID "Test_01"

#define WIFI_CONNECTED_BIT BIT0
#define TIME_SYNCED_BIT    BIT1

static EventGroupHandle_t s_app_event_group = NULL;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;
static TaskHandle_t s_main_task_handle = NULL;

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

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started, attempting to connect...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        s_mqtt_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_app_event_group, WIFI_CONNECTED_BIT);
    }
}

static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "System time synchronized via SNTP.");
    xEventGroupSetBits(s_app_event_group, TIME_SYNCED_BIT);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker successfully.");
            s_mqtt_connected = true;
            s_mqtt_client = event->client;
            
            char status_topic[128];
            snprintf(status_topic, sizeof(status_topic), "registry/%s/status", DEVICE_ID);
            esp_mqtt_client_publish(s_mqtt_client, status_topic, "online", 0, 1, 0);
            ESP_LOGI(TAG, "Published online status to %s", status_topic);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected from broker.");
            s_mqtt_connected = false;
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT connection error occurred.");
            break;
            
        default:
            break;
    }
}

static void report_timer_callback(TimerHandle_t xTimer) {
    if (s_main_task_handle != NULL) {
        xTaskNotifyGive(s_main_task_handle);
    }
}

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "System booting up...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_app_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

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

    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    xEventGroupWaitBits(s_app_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "Initializing SNTP for TLS certificate validation...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    ESP_LOGI(TAG, "Waiting for time synchronization...");
    xEventGroupWaitBits(s_app_event_group, TIME_SYNCED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "Starting MQTT Client...");
    
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

    s_main_task_handle = xTaskGetCurrentTaskHandle();

    TimerHandle_t report_timer = xTimerCreate("report_timer", pdMS_TO_TICKS(15000), pdTRUE, (void *)0, report_timer_callback);
    if (report_timer != NULL) {
        xTimerStart(report_timer, 0);
        ESP_LOGI(TAG, "15-second reporting timer started.");
    }

    uint32_t message_counter = 0;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (s_mqtt_connected) {
            char publish_topic[128];
            char payload[128];
            
            snprintf(publish_topic, sizeof(publish_topic), "device/%s/msg", DEVICE_ID);
            snprintf(payload, sizeof(payload), "{\"count\": %lu}", (unsigned long)message_counter);
            
            esp_mqtt_client_publish(s_mqtt_client, publish_topic, payload, 0, 1, 0);
            ESP_LOGI(TAG, "Successfully reported data: %s", payload);
            
            message_counter++;
        } else {
            ESP_LOGW(TAG, "MQTT is disconnected. Data reporting bypassed.");
        }
    }
}
