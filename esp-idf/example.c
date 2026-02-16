#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_sntp.h"
#include "time.h"

static const char *TAG = "MqttClient";

// ================= 配置区域 =================

// WiFi 配置
const char* wifiSsid = "YOUR_WIFI_SSID";
const char* wifiPass = "YOUR_WIFI_PASSWORD";

// 显示在最终设备列表中的设备id
const char* deviceId = "esp32_dev01";

// 引用代码末尾的证书内容
extern const char *serverCert;

// ================= 全局变量 =================

esp_mqtt_client_handle_t globalMqttClient = NULL;
bool isMqttConnected = false;

// ================= 工具函数接口 =================

/**
 * @brief 一键修正本地时间 (NTP)
 * 使用 aelecti.top 作为 NTP 服务器
 */
void syncTime(void) {
   ESP_LOGI(TAG, "正在初始化 NTP ...");

   // 防止重复初始化
   sntp_stop();
   sntp_setoperatingmode(SNTP_OPMODE_POLL);
   sntp_setservername(0, "aelecti.top");
   sntp_init();

   // 设置时区为中国标准时间 (CST-8)
   setenv("TZ", "CST-8", 1);
   tzset();

   // 等待时间同步
   int retry = 0;
   const int max_retry = 10;
   while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= max_retry) {
      ESP_LOGI(TAG, "等待时间同步... (%d/%d)", retry, max_retry);
      vTaskDelay(pdMS_TO_TICKS(2000));
   }

   if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      time_t now;
      struct tm timeinfo;
      time(&now);
      localtime_r(&now, &timeinfo);

      char strftime_buf[64];
      strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
      ESP_LOGI(TAG, "时间同步成功: %s", strftime_buf);
   } else {
      ESP_LOGW(TAG, "时间同步超时，使用默认时间");
   }
}

/**
 * 发送一条字符串到服务器
 *
 * @param message 消息内容
 */
void mqttLog(const char* message) {
   mqttPublish("log", message);
}

/**
 * 发送一次指定话题的字符串数据
 * 会自动拼接Topic前缀: "device/{deviceId}/{subTopic}"
 *
 * @param subTopic 子主题，例如传入 "data" 则实际发送到 "device/esp32_dev01/data"
 * @param data 要发送的字符串内容
 */
void mqttPublish(const char* subTopic, const char* data) {
   if (!globalMqttClient || !isMqttConnected) {
      ESP_LOGW(TAG, "MQTT未连接，忽略发送: %s", subTopic);
      return;
   }

   char fullTopic[128];
   // 格式化 Topic: device/esp32_dev01/xxx
   snprintf(fullTopic, sizeof(fullTopic), "device/%s/%s", deviceId, subTopic);

   // qos=1: 确保至少送达一次; retain=0: 不保留
   int msgId = esp_mqtt_client_publish(globalMqttClient, fullTopic, data, 0, 1, 0);
   ESP_LOGI(TAG, "发送至 [%s], msg_id=%d, 内容: %s", fullTopic, msgId, data);
}

// ================= 内部逻辑 =================

// 处理接收到的数据
void handleIncomingMessage(const char* topic, int topicLen, const char* data, int dataLen) {
   // 1. 本地串口打印，方便调试
   ESP_LOGI(TAG, "收到消息 Topic: %.*s", topicLen, topic);
   ESP_LOGI(TAG, "消息内容: %.*s", dataLen, data);

   // 2. 简单的回声逻辑：告诉服务器我收到了什么
   // 发送到 device/{deviceId}/echo
   char echoBuf[256];
   snprintf(echoBuf, sizeof(echoBuf), "ESP32 Recv: %.*s", dataLen, data);
   mqttPublish("resp", echoBuf);
}

static void mqttEventHandler(void *handlerArgs, esp_event_base_t base, int32_t eventId, void *eventData) {
   esp_mqtt_event_handle_t event = eventData;
   esp_mqtt_client_handle_t client = event->client;

   switch ((esp_mqtt_event_id_t)eventId) {
      case MQTT_EVENT_CONNECTED:
         ESP_LOGI(TAG, "MQTT 已连接服务器");
         isMqttConnected = true;
         globalMqttClient = client;

         // 1. 订阅指令 Topic: device/{deviceId}/cmd
         char topicCmd[64];
         snprintf(topicCmd, sizeof(topicCmd), "device/%s/cmd", deviceId);
         esp_mqtt_client_subscribe(client, topicCmd, 1);
         ESP_LOGI(TAG, "已订阅: %s", topicCmd);

         // 2. 发送上线通知
         char topicStatus[64];
         snprintf(topicStatus, sizeof(topicStatus), "registry/%s/status", deviceId);
         // payload: "online"
         esp_mqtt_client_publish(client, topicStatus, "online", 0, 1, 0);
         ESP_LOGI(TAG, "已发送上线通知至: %s", topicStatus);
         break;

      case MQTT_EVENT_DISCONNECTED:
         ESP_LOGI(TAG, "MQTT 已断开连接");
         isMqttConnected = false;
         break;

      case MQTT_EVENT_DATA:
         // 收到任何订阅的消息时触发
         handleIncomingMessage(event->topic, event->topic_len, event->data, event->data_len);
         break;

      case MQTT_EVENT_ERROR:
         ESP_LOGE(TAG, "MQTT 发生错误");
         if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "TLS 错误 - 请检查: 1.证书有效期 2.本地系统时间 3.服务器防火墙 8883 端口");
            ESP_LOGE(TAG, "Last ESP Err: 0x%x, Stack Err: 0x%x",
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_tls_stack_err);
         }
         break;
      default:
         break;
   }
}

static void mqttAppStart(void) {
   const esp_mqtt_client_config_t mqttCfg = {
      .broker.address.uri = "mqtts://aelecti.top:8883", // MQTTS
      .broker.verification.certificate = serverCert,    // aelecti.top证书，于文件尾部定义
      .session.keepalive = 60,
      .credentials.client_id = deviceId,
   };

   esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqttCfg);
   esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqttEventHandler, client);
   esp_mqtt_client_start(client);
}

// 简易的演示用wifi连接函数
void wifiInitSta(void) {
   esp_netif_create_default_wifi_sta();
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   esp_wifi_init(&cfg);

   wifi_config_t wifiConfig = {0};
   strncpy((char*)wifiConfig.sta.ssid, wifiSsid, sizeof(wifiConfig.sta.ssid));
   strncpy((char*)wifiConfig.sta.password, wifiPass, sizeof(wifiConfig.sta.password));

   // 增强连接稳定性
   wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

   esp_wifi_set_mode(WIFI_MODE_STA);
   esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);
   esp_wifi_start();

   ESP_LOGI(TAG, "正在连接 WiFi: %s ...", wifiSsid);
   esp_wifi_connect();

   // 简单延时等待连接，实际项目中建议使用 EventGroup
   vTaskDelay(pdMS_TO_TICKS(5000));
}

void app_main(void) {
   ESP_LOGI(TAG, "[APP] 启动...");

   // 初始化 NVS
   esp_err_t ret = nvs_flash_init();
   if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
   }
   ESP_ERROR_CHECK(ret);

   ESP_ERROR_CHECK(esp_netif_init());
   ESP_ERROR_CHECK(esp_event_loop_create_default());

   // 1. 连接 WiFi
   wifiInitSta();

   // 2. 同步时间
   syncTime();

   // 3. 启动 MQTT
   mqttAppStart();

   // 4. 主循环：仅作心跳保活演示
   int heartbeatCount = 0;
   char msgBuf[64];

   while (1) {
      if (isMqttConnected) {
         // 每隔一段时间发送心跳包，证明设备还活着
         snprintf(msgBuf, sizeof(msgBuf), "Heartbeat #%d, Heap: %lu",
                  heartbeatCount++, esp_get_free_heap_size());

         mqttPublish("msg", msgBuf);
      }

      // 10秒一次
      vTaskDelay(pdMS_TO_TICKS(10000));
   }
}

// ================= 证书 =================
// 启用TLS以防止与服务器明文通信，此为aelecti.top的证书
const char *serverCert =
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