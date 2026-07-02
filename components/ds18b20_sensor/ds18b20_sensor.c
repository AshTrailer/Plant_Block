#include "ds18b20_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "cloud_comm.h"
#include "time_manager.h"
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

// 11 位分辨率转换时间：375ms（手册标称），加 5ms 余量
#define DS18B20_CONV_DELAY_MS    380

// ---------- 静态状态 ----------
static onewire_bus_handle_t      s_bus = NULL;
static ds18b20_device_handle_t   s_devices[DS18B20_MAX_DEVICES];
static int                       s_device_count = 0;

// 连续采集定时器
static TimerHandle_t             s_periodic_timer = NULL;   // 周期触发转换的定时器
static TimerHandle_t             s_readback_timer = NULL;   // 转换完成后读取的单次定时器
static uint32_t                  s_period_ms = 5000;

// 单次异步读取的回调暂存
static ds18b20_temp_cb_t         s_once_callback = NULL;
static void                     *s_once_user_ctx = NULL;

// ---------- 内部辅助：读取所有设备并通知 ----------
static void read_all_devices_and_notify(ds18b20_temp_cb_t callback, void *user_ctx)
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
            callback(i, -999.0f, user_ctx);  // 用异常值通知失败
         }
      }
   }
}

// ---------- 读回定时器回调（转换完成 → 读取数据）----------
static void readback_timer_cb(TimerHandle_t timer)
{
   // 判断是连续采集还是单次异步
   ds18b20_temp_cb_t cb = s_once_callback;
   void *ctx = s_once_user_ctx;

   // 清除单次回调（一次性）
   s_once_callback = NULL;
   s_once_user_ctx = NULL;

   read_all_devices_and_notify(cb, ctx);
}

// ---------- 周期定时器回调（触发转换 → 启动读回定时器）----------
static void periodic_timer_cb(TimerHandle_t timer)
{
   esp_err_t err = ds18b20_trigger_temperature_conversion_for_all(s_bus);
   if (err != ESP_OK) {
      DS18B20_LOGE("Periodic: trigger conversion failed (err=%d)", err);
      return;
   }

   // 启动读回定时器（单次，380ms 后读取）
   xTimerChangePeriod(s_readback_timer, pdMS_TO_TICKS(DS18B20_CONV_DELAY_MS), 0);
   xTimerStart(s_readback_timer, 0);
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
      .flags = {
         .en_pull_up = false,   // 外部 4.7kΩ 上拉
      }
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

   // --- 3. 创建定时器 ---
   // 读回定时器：单次模式，初始不启动
   s_readback_timer = xTimerCreate(
      "ds18b20_readback",
      pdMS_TO_TICKS(DS18B20_CONV_DELAY_MS),
      pdFALSE,    // 单次触发
      NULL,
      readback_timer_cb
   );

   // 周期定时器：由 start_reading 启动
   s_periodic_timer = xTimerCreate(
      "ds18b20_periodic",
      pdMS_TO_TICKS(s_period_ms),
      pdTRUE,     // 自动重载
      NULL,
      periodic_timer_cb
   );
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

// ---------- 阻塞版全量读取（仅用于测试/初始化）----------
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

   // 阻塞等待转换完成（仅在可接受阻塞的上下文中调用）
   vTaskDelay(pdMS_TO_TICKS(DS18B20_CONV_DELAY_MS));

   bool all_ok = true;
   for (int i = 0; i < s_device_count; i++) {
      if (!ds18b20_sensor_get_temperature(i, &temperatures[i])) {
         all_ok = false;
      }
   }
   return all_ok;
}

// ---------- 非阻塞单次异步读取 ----------
void ds18b20_sensor_read_once_async(ds18b20_temp_cb_t callback, void *user_ctx)
{
   if (s_device_count == 0) {
      DS18B20_LOGW("read_once_async: no devices");
      return;
   }

   esp_err_t err = ds18b20_trigger_temperature_conversion_for_all(s_bus);
   if (err != ESP_OK) {
      DS18B20_LOGE("read_once_async: trigger failed (err=%d)", err);
      return;
   }

   // 暂存回调，供 readback_timer_cb 使用
   s_once_callback = callback;
   s_once_user_ctx = user_ctx;

   // 启动读回定时器
   xTimerChangePeriod(s_readback_timer, pdMS_TO_TICKS(DS18B20_CONV_DELAY_MS), 0);
   xTimerStart(s_readback_timer, 0);
}

// ---------- 手动触发转换（不读取）----------
void ds18b20_sensor_trigger_conversion(void)
{
   if (s_device_count == 0) return;
   esp_err_t err = ds18b20_trigger_temperature_conversion_for_all(s_bus);
   if (err != ESP_OK) {
      DS18B20_LOGE("trigger_conversion failed: %d", err);
   }
}

// ---------- ROM 地址 ----------
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

// ---------- 连续采集 ----------
void ds18b20_sensor_start_reading(uint32_t interval_ms)
{
   if (s_device_count == 0) {
      DS18B20_LOGE("Cannot start: no DS18B20 devices");
      return;
   }

   // 确保间隔不小于转换时间 + 余量
   if (interval_ms < DS18B20_CONV_DELAY_MS + 500) {
      interval_ms = DS18B20_CONV_DELAY_MS + 500;
      DS18B20_LOGW("Interval too short, adjusted to %lu ms", (unsigned long)interval_ms);
   }

   s_period_ms = interval_ms;

   // 修改周期并启动
   xTimerChangePeriod(s_periodic_timer, pdMS_TO_TICKS(s_period_ms), 0);
   xTimerStart(s_periodic_timer, 0);

   DS18B20_LOGI("Continuous reading started (interval: %lu ms, non-blocking timer-driven)", 
                (unsigned long)s_period_ms);
}

void ds18b20_sensor_stop_reading(void)
{
   xTimerStop(s_periodic_timer, 0);
   xTimerStop(s_readback_timer, 0);
   DS18B20_LOGI("Continuous reading stopped");
}