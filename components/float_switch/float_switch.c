#include "float_switch.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.h"

static const char *TAG = "FLOAT_SWITCH";

// ---------- 静态状态 ----------
static int        s_gpio_num = -1;
static bool       s_initialized = false;

// 监测任务
static TaskHandle_t s_monitor_task = NULL;
static uint32_t   s_interval_ms = 500;

// ---------- 弱回调（用户可覆盖） ----------
__attribute__((weak))
void float_switch_on_state_changed(bool state)
{
   ESP_LOGI(TAG, "Float switch: %s", state ? "有水" : "缺水");
   // 通过事件总线发布，irrigation_controller 可订阅
   event_bus_publish(EVENT_SENSOR_FLOAT_SW, &state, sizeof(state));
}

// ---------- 监测任务 ----------
static void float_switch_monitor_task(void *arg)
{
   bool last_state = float_switch_get_state();
   while (1) {
      vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
      bool current = float_switch_get_state();
      if (current != last_state) {
         last_state = current;
         // 调用内部回调（默认打日志，用户可覆盖）
         float_switch_on_state_changed(current);
      }
   }
}

// ===================================================================
//  公开 API
// ===================================================================

void float_switch_init(int gpio_num)
{
   s_gpio_num = gpio_num;

   gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << gpio_num),
      .mode         = GPIO_MODE_INPUT,
      .pull_up_en   = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,   // 外部已有 10k 下拉
      .intr_type    = GPIO_INTR_DISABLE,
   };
   gpio_config(&io_conf);

   s_initialized = true;
   ESP_LOGI(TAG, "Float switch initialized on GPIO%d", gpio_num);
}

bool float_switch_get_state(void)
{
   if (!s_initialized) {
      return false;
   }
   // 高电平 = 浮球闭合 = 有水
   return (gpio_get_level(s_gpio_num) == 1);
}

void float_switch_start_monitor(uint32_t interval_ms)
{
   if (!s_initialized) {
      ESP_LOGE(TAG, "Not initialized, call float_switch_init() first");
      return;
   }

   if (s_monitor_task != NULL) {
      ESP_LOGW(TAG, "Monitor already running, stopping old one");
      float_switch_stop_monitor();
   }

   s_interval_ms = (interval_ms < 50) ? 50 : interval_ms;

   xTaskCreate(
      float_switch_monitor_task,
      "float_switch_mon",
      2048,
      NULL,
      2,
      &s_monitor_task
   );

   ESP_LOGI(TAG, "Monitor started (interval: %lu ms)", (unsigned long)s_interval_ms);
}

void float_switch_stop_monitor(void)
{
   if (s_monitor_task != NULL) {
      vTaskDelete(s_monitor_task);
      s_monitor_task = NULL;
      ESP_LOGI(TAG, "Monitor stopped");
   }
}