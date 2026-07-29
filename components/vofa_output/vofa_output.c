#include "vofa_output.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "VOFA_OUTPUT";

static int s_uart_num = UART_NUM_0;

// ---------- 初始化 ----------
void vofa_output_init(int uart_num, int baud_rate)
{
   s_uart_num = uart_num;
   /* 检查驱动是否已安装（例如被控制台占用）。
      对 UART0：系统启动后日志/输入用的是 ROM 函数，驱动通常未安装。 */
   if (!uart_is_driver_installed(uart_num)) {
      uart_config_t uart_cfg = {
         .baud_rate = baud_rate,
         .data_bits = UART_DATA_8_BITS,
         .parity    = UART_PARITY_DISABLE,
         .stop_bits = UART_STOP_BITS_1,
         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
         .source_clk = UART_SCLK_APB,
      };
      ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_cfg));
      /* TX only：2048 字节发送缓冲，不占用 RX（保留给 getchar() 的 ROM 轮询） */
      ESP_ERROR_CHECK(uart_driver_install(uart_num, 2048, 0, 0, NULL, 0));
      ESP_LOGI(TAG, "UART%d driver installed (TX only)", uart_num);
   } else {
      ESP_LOGI(TAG, "UART%d driver already installed, reusing", uart_num);
   }
   ESP_LOGI(TAG, "Vofa+ output initialized, UART%d @ %d bps", uart_num, baud_rate);
}

// ---------- 可变参数发送 ----------
void vofa_output_send(const char *prefix, const char *fmt, ...)
{
   char buf[256];
   int pos = 0;

   // 前缀（可选）
   if (prefix != NULL && prefix[0] != '\0') {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "%s:", prefix);
   }

   // 格式化数据
   va_list args;
   va_start(args, fmt);
   pos += vsnprintf(buf + pos, sizeof(buf) - pos, fmt, args);
   va_end(args);

   // 追加换行符（Vofa+ FireWater 协议要求）
   if (pos < sizeof(buf) - 2) {
      buf[pos++] = '\n';
      buf[pos] = '\0';
   } else {
      buf[sizeof(buf) - 2] = '\n';
      buf[sizeof(buf) - 1] = '\0';
   }

   // 通过 UART 发送
   uart_write_bytes(s_uart_num, buf, strlen(buf));
}

// ---------- 便捷传感器数据帧 ----------
void vofa_output_send_sensor_frame(float sht30_temp, float sht30_hum,
                                   float ds18_cold, float ds18_hot,
                                   float moisture_pct, uint32_t moisture_mv,
                                   bool float_has_water, bool ntc_overtemp,
                                   uint8_t light_duty, bool light_on,
                                   uint8_t pump_speed, bool pump_on)
{
   vofa_output_send("sensors",
      "%.2f,%.2f,%.2f,%.2f,%.1f,%lu,%d,%d,%d,%d,%d,%d",
      sht30_temp, sht30_hum,
      ds18_cold, ds18_hot,
      moisture_pct, (unsigned long)moisture_mv,
      float_has_water ? 1 : 0,
      ntc_overtemp ? 1 : 0,
      light_duty, light_on ? 1 : 0,
      pump_speed, pump_on ? 1 : 0);
}

// ---------- 事件订阅（可选，供系统监控用）----------
static void event_to_vofa(event_type_t type, const void *data, size_t len, void *user_ctx)
{
   (void)len;
   (void)user_ctx;

   switch (type) {
      case EVENT_ALARM: {
         const char *msg = (const char *)data;
         vofa_output_send("alarm", "%s", msg ? msg : "unknown");
         break;
      }
      case EVENT_SYSTEM_HEALTH: {
         const uint32_t *d = (const uint32_t *)data;
         vofa_output_send("health", "free_heap:%lu,min_stack:%lu",
                          (unsigned long)d[0], (unsigned long)d[1]);
         break;
      }
      default:
         break;
   }
}

void vofa_output_subscribe_all(void)
{
   event_bus_subscribe(EVENT_ALARM, event_to_vofa, NULL);
   event_bus_subscribe(EVENT_SYSTEM_HEALTH, event_to_vofa, NULL);
   ESP_LOGI(TAG, "Subscribed to event bus for Vofa+ output");
}