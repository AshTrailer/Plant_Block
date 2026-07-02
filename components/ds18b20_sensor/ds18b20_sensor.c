#include "ds18b20_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "cloud_comm.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DS18B20_SENSOR";

// ---------- 日志宏 ----------
#define DS18B20_LOGI(fmt, ...) do { \
    ESP_LOGI(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[I] " fmt, ##__VA_ARGS__); \
} while(0)

#define DS18B20_LOGE(fmt, ...) do { \
    ESP_LOGE(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[E] " fmt, ##__VA_ARGS__); \
} while(0)

#define DS18B20_LOGW(fmt, ...) do { \
    ESP_LOGW(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[W] " fmt, ##__VA_ARGS__); \
} while(0)

// ---------- 配置 ----------
#define DS18B20_MAX_DEVICES      4
#define DS18B20_CONV_DELAY_MS    380   // 11-bit: 375ms + 5ms 余量

// ---------- 静态状态 ----------
static onewire_bus_handle_t      s_bus = NULL;
static ds18b20_device_handle_t   s_devices[DS18B20_MAX_DEVICES];
static int                       s_device_count = 0;

// 工作线程
static TaskHandle_t              s_worker_task = NULL;

// 周期定时器
static TimerHandle_t             s_periodic_timer = NULL;
static uint32_t                  s_period_ms = 5000;

// 运行模式
static volatile bool             s_continuous_mode = false;   // true: 自循环最快模式
static volatile bool             s_running = false;           // 采集是否运行中

// 单次异步回调暂存
static ds18b20_temp_cb_t         s_once_callback = NULL;
static void                     *s_once_user_ctx = NULL;

// ---------- 内部：读取所有设备并通知回调 ----------
static void read_all_and_notify(ds18b20_temp_cb_t callback, void *user_ctx)
{
   for (int i = 0; i < s_device_count; i++) {
      float temp = 0.0f;
      esp_err_t err = ds18b20_get_temperature(s_devices[i], &temp);
      if (err == ESP_OK) {
         DS18B20_LOGI("DS18B20[%d]: %.2f °C", i, temp);
         if (callback) {
            callback(i, temp, user_ctx);
         }
      } else {
         DS18B20_LOGE("DS18B20[%d]: read failed (err=%d)", i, err);
         if (callback) {
            callback(i, -999.0f, user_ctx);
         }
      }
   }
}

// ---------- 执行一次完整的"触发 → 等待 → 读取"循环 ----------
static void do_one_cycle(void)
{
   esp_err_t err = ds18b20_trigger_temperature_conversion_for_all(s_bus);
   if (err != ESP_OK) {
      DS18B20_LOGE("Worker: trigger conversion failed (err=%d)", err);
      return;
   }

   // 等待转换完成（只阻塞本任务）
   vTaskDelay(pdMS_TO_TICKS(DS18B20_CONV_DELAY_MS));

   // 消费一次性回调（如果有的话），否则用 NULL 只打日志
   ds18b20_temp_cb_t cb = s_once_callback;
   void *ctx = s_once_user_ctx;
   s_once_callback = NULL;
   s_once_user_ctx = NULL;

   read_all_and_notify(cb, ctx);
}

// ---------- 工作线程 ----------
static void ds18b20_worker_task(void *arg)
{
   DS18B20_LOGI("Worker task started, waiting for trigger...");

   while (1) {
      if (s_continuous_mode) {
         // ========== 连续模式：自循环，最快速度 ==========
         while (s_continuous_mode) {
            do_one_cycle();
         }
         // 退出连续模式后，回到等待通知状态
         DS18B20_LOGI("Continuous mode stopped, back to idle");
      }

      // ========== 周期模式 / 空闲：等待通知 ==========
      // 清空积压通知后等待下一个
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      // 收到通知但可能已被切到连续模式（竞态），此时跳过单次执行
      if (!s_continuous_mode) {
         do_one_cycle();
      }
   }
}

// ---------- 周期定时器回调（极轻量）----------
static void periodic_timer_cb(TimerHandle_t timer)
{
   if (s_worker_task != NULL && !s_continuous_mode) {
      // 连续模式下周期定时器不参与，避免无效通知
      xTaskNotifyGive(s_worker_task);
   }
}

// ===================================================================
//  公开 API
// ===================================================================

void ds18b20_sensor_init(int gpio_num, int max_devices)
{
   if (max_devices > DS18B20_MAX_DEVICES) {
      max_devices = DS18B20_MAX_DEVICES;
   }

   // --- 1. 安装 1-Wire 总线（RMT 后端）---
   onewire_bus_config_t bus_config = {
      .bus_gpio_num = gpio_num,
      .flags = { .en_pull_up = false }
   };
   onewire_bus_rmt_config_t rmt_config = {
      .max_rx_bytes = 10,
   };
   ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &s_bus));
   DS18B20_LOGI("1-Wire bus installed on GPIO%d (RMT backend, external pull-up)", gpio_num);

   // --- 2. 搜索设备 ---
   onewire_device_iter_handle_t iter = NULL;
   ESP_ERROR_CHECK(onewire_new_device_iter(s_bus, &iter));
   DS18B20_LOGI("Searching for DS18B20 devices...");

   onewire_device_t next_device;
   esp_err_t search_result;

   do {
      search_result = onewire_device_iter_get_next(iter, &next_device);
      if (search_result == ESP_OK) {
         ds18b20_config_t ds_cfg = {};
         ds18b20_device_handle_t handle = NULL;
         if (ds18b20_new_device_from_enumeration(&next_device, &ds_cfg, &handle) == ESP_OK) {
            if (s_device_count < max_devices) {
               s_devices[s_device_count] = handle;

               esp_err_t res_err = ds18b20_set_resolution(handle, DS18B20_RESOLUTION_11B);
               if (res_err == ESP_OK) {
                  DS18B20_LOGI("Found DS18B20[%d], resolution set to 11-bit", s_device_count);
               } else {
                  DS18B20_LOGW("DS18B20[%d]: set 11-bit failed (err=%d)", s_device_count, res_err);
               }

               onewire_device_address_t addr;
               ds18b20_get_device_address(handle, &addr);
               DS18B20_LOGI("  ROM: %016llX", addr);

               s_device_count++;
            } else {
               ds18b20_del_device(handle);
            }
         } else {
            DS18B20_LOGI("Unknown device: %016llX", next_device.address);
         }
      }
   } while (search_result != ESP_ERR_NOT_FOUND);

   ESP_ERROR_CHECK(onewire_del_device_iter(iter));
   DS18B20_LOGI("Search done: %d DS18B20 device(s) found", s_device_count);

   if (s_device_count == 0) {
      DS18B20_LOGE("No DS18B20 found! Check wiring / pull-up resistor.");
      return;
   }

   // --- 3. 创建工作线程 ---
   xTaskCreate(
      ds18b20_worker_task,
      "ds18b20_worker",
      4096,
      NULL,
      3,
      &s_worker_task
   );

   // --- 4. 创建周期定时器（不启动）---
   s_periodic_timer = xTimerCreate(
      "ds18b20_periodic",
      pdMS_TO_TICKS(s_period_ms),
      pdTRUE,
      NULL,
      periodic_timer_cb
   );

   DS18B20_LOGI("DS18B20 sensor module initialized");
}

int ds18b20_sensor_get_device_count(void)
{
   return s_device_count;
}

bool ds18b20_sensor_get_temperature(int index, float *temperature)
{
   if (index < 0 || index >= s_device_count || temperature == NULL) {
      return false;
   }
   esp_err_t err = ds18b20_get_temperature(s_devices[index], temperature);
   return (err == ESP_OK);
}

bool ds18b20_sensor_get_all_temperatures_blocking(float *temperatures)
{
   if (s_device_count == 0) {
      DS18B20_LOGW("No devices");
      return false;
   }

   esp_err_t err = ds18b20_trigger_temperature_conversion_for_all(s_bus);
   if (err != ESP_OK) {
      DS18B20_LOGE("Trigger conversion failed: %d", err);
      return false;
   }

   vTaskDelay(pdMS_TO_TICKS(DS18B20_CONV_DELAY_MS));

   bool all_ok = true;
   for (int i = 0; i < s_device_count; i++) {
      if (!ds18b20_sensor_get_temperature(i, &temperatures[i])) {
         all_ok = false;
      }
   }
   return all_ok;
}

void ds18b20_sensor_read_once_async(ds18b20_temp_cb_t callback, void *user_ctx)
{
   if (s_device_count == 0 || s_worker_task == NULL) {
      DS18B20_LOGW("read_once_async: no devices or worker not ready");
      return;
   }

   s_once_callback = callback;
   s_once_user_ctx = user_ctx;

   if (!s_continuous_mode) {
      xTaskNotifyGive(s_worker_task);
   }
   // 连续模式下不需要发通知，下一次循环会自动消费 s_once_callback
}

void ds18b20_sensor_trigger_conversion(void)
{
   if (s_device_count == 0) return;
   esp_err_t err = ds18b20_trigger_temperature_conversion_for_all(s_bus);
   if (err != ESP_OK) {
      DS18B20_LOGE("trigger_conversion failed: %d", err);
   }
}

bool ds18b20_sensor_get_device_address_str(int index, char *buffer, size_t len)
{
   if (index < 0 || index >= s_device_count || buffer == NULL || len < 17) {
      return false;
   }
   onewire_device_address_t addr;
   ds18b20_get_device_address(s_devices[index], &addr);
   snprintf(buffer, len, "%016llX", addr);
   return true;
}

// ---------- 周期采集 ----------
void ds18b20_sensor_start_reading(uint32_t interval_ms)
{
   if (s_device_count == 0) {
      DS18B20_LOGE("Cannot start: no DS18B20 devices");
      return;
   }

   // 退出连续模式（如果在其中）
   s_continuous_mode = false;

   // 最小间隔 = 转换时间 + 少量通信余量
   if (interval_ms < DS18B20_CONV_DELAY_MS + 20) {
      interval_ms = DS18B20_CONV_DELAY_MS + 20;
      DS18B20_LOGW("Interval too short, adjusted to %lu ms (min for 11-bit)",
                   (unsigned long)interval_ms);
   }

   s_period_ms = interval_ms;
   s_running = true;

   xTimerChangePeriod(s_periodic_timer, pdMS_TO_TICKS(s_period_ms), 0);
   xTimerStart(s_periodic_timer, 0);

   DS18B20_LOGI("Periodic reading started (interval: %lu ms)", (unsigned long)s_period_ms);
}

// ---------- 最快连续采集 ----------
void ds18b20_sensor_start_continuous(void)
{
   if (s_device_count == 0) {
      DS18B20_LOGE("Cannot start continuous: no DS18B20 devices");
      return;
   }

   // 停止周期定时器（连续模式不需要它）
   xTimerStop(s_periodic_timer, 0);

   s_running = true;
   s_continuous_mode = true;

   // 唤醒 worker（如果它在等待通知）
   if (s_worker_task != NULL) {
      xTaskNotifyGive(s_worker_task);
   }

   DS18B20_LOGI("Continuous fastest reading started (~%d ms/cycle)", DS18B20_CONV_DELAY_MS);
}

void ds18b20_sensor_stop_reading(void)
{
   s_continuous_mode = false;
   s_running = false;
   xTimerStop(s_periodic_timer, 0);
   DS18B20_LOGI("Reading stopped");
}