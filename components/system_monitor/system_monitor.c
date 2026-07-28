#include "system_monitor.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include <string.h>

static const char *TAG = "SYS_MON";

#define MAX_REGISTERED_TASKS 16

typedef struct {
   const char *name;
   TaskHandle_t handle;
} task_entry_t;

static task_entry_t s_tasks[MAX_REGISTERED_TASKS];
static int s_task_count = 0;
static uint32_t s_interval_ms = 10000;
static uint32_t s_stack_threshold = 256;  // 字（1024 字节）
static uint32_t s_min_free_heap = UINT32_MAX;
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_monitor_task_handle = NULL;

// ---------- 监控任务 ----------
static void monitor_task(void *arg)
{
   esp_task_wdt_add(NULL);
   ESP_LOGI(TAG, "System monitor started (interval=%lu ms, stack_threshold=%lu words)",
            (unsigned long)s_interval_ms, (unsigned long)s_stack_threshold);

   while (1) {
      vTaskDelay(pdMS_TO_TICKS(s_interval_ms));

      // 1. 堆内存
      uint32_t free_heap = esp_get_free_heap_size();
      if (free_heap < s_min_free_heap) {
         s_min_free_heap = free_heap;
      }

      // 2. 各任务堆栈水位
      bool stack_alert = false;
      if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
         for (int i = 0; i < s_task_count; i++) {
            if (s_tasks[i].handle == NULL) continue;
            UBaseType_t high_water = uxTaskGetStackHighWaterMark(s_tasks[i].handle);
            if (high_water < s_stack_threshold) {
               ESP_LOGW(TAG, "Task '%s' stack low! HighWater=%lu words (<%lu)",
                        s_tasks[i].name,
                        (unsigned long)high_water,
                        (unsigned long)s_stack_threshold);
               stack_alert = true;
            }
         }
         xSemaphoreGive(s_mutex);
      }

      // 3. 发布系统健康事件
      uint32_t health_data[2] = { free_heap, s_min_free_heap };
      event_bus_publish(EVENT_SYSTEM_HEALTH, health_data, sizeof(health_data));

      // 4. 定期日志（降频到每 60 秒一次）
      static int log_counter = 0;
      if (++log_counter >= (60000 / s_interval_ms)) {
         log_counter = 0;
         ESP_LOGI(TAG, "Heap: free=%lu, min_free=%lu, tasks=%d%s",
                  (unsigned long)free_heap, (unsigned long)s_min_free_heap,
                  s_task_count, stack_alert ? " [STACK ALERT!]" : "");
      }
      esp_task_wdt_reset();
   }
}

// ---------- 初始化 ----------
// ============ system_monitor_init 替换 ============
void system_monitor_init(uint32_t interval_ms, uint32_t stack_low_water_threshold)
{
   s_interval_ms = interval_ms;
   s_stack_threshold = stack_low_water_threshold;
   s_mutex = xSemaphoreCreateMutex();

   /* ESP-IDF v5.x 在早期启动时已自动初始化 TWDT（超时 5s，监控双核 IDLE）。
      先销毁默认配置，再用我们的参数重建。 */
   esp_task_wdt_deinit();   // 忽略返回值

   esp_task_wdt_config_t twdt_cfg = {
      .timeout_ms = 30000,
      .idle_core_mask = (1 << 0) | (1 << 1),  // 监控 Core 0 和 Core 1 的 IDLE 任务（最后防线）
      .trigger_panic = true,
   };
   ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_cfg));

   /* 将当前任务（main）订阅到 TWDT，main 循环中须定期 system_monitor_feed_watchdog() */
   esp_task_wdt_add(NULL);
   ESP_LOGI(TAG, "TWDT reconfigured: timeout=%lu ms, idle_core_mask=0x%x",
            (unsigned long)twdt_cfg.timeout_ms, twdt_cfg.idle_core_mask);

   /* 启动监控任务 */
   xTaskCreate(monitor_task, "sys_monitor", 3072, NULL,
               tskIDLE_PRIORITY + 2, &s_monitor_task_handle);

   /* 注册自身并添加到 TWDT */
   system_monitor_register_task("sys_monitor", s_monitor_task_handle);

   ESP_LOGI(TAG, "System monitor initialized");
}

// ---------- 注册任务 ----------
void system_monitor_register_task(const char *name, void *task_handle)
{
   if (s_mutex == NULL) return;

   xSemaphoreTake(s_mutex, portMAX_DELAY);
   if (s_task_count < MAX_REGISTERED_TASKS) {
      s_tasks[s_task_count].name = name;
      s_tasks[s_task_count].handle = (TaskHandle_t)task_handle;
      s_task_count++;
      ESP_LOGI(TAG, "Registered task '%s' for stack monitoring", name);
   }
   xSemaphoreGive(s_mutex);
}

// ---------- 喂狗 ----------
void system_monitor_feed_watchdog(void)
{
   esp_task_wdt_reset();
}

// ---------- 查询 ----------
uint32_t system_monitor_get_free_heap(void)
{
   return esp_get_free_heap_size();
}

uint32_t system_monitor_get_min_free_heap(void)
{
   return s_min_free_heap;
}