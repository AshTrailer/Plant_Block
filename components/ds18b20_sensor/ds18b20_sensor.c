#include "ds18b20_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"          // ← 新增：互斥锁需要
#include "onewire_bus.h"
#include "ds18b20.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DS18B20_SENSOR";

/* ---------- 日志宏 ---------- */
#define DS18B20_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define DS18B20_LOGE(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#define DS18B20_LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)

/* ---------- 配置 ---------- */
#define DS18B20_MAX_DEVICES      4
#define DS18B20_CONV_DELAY_MS    380   // 11-bit: 375ms + 5ms 余量

/* ---------- 设备角色 ---------- */
static char s_device_roles[DS18B20_MAX_DEVICES][8] = {
   "cold", "hot", "", ""
};

/* ---------- 静态状态 ---------- */
static onewire_bus_handle_t      s_bus = NULL;
static ds18b20_device_handle_t   s_devices[DS18B20_MAX_DEVICES];
static int                       s_device_count = 0;
static onewire_device_address_t  s_device_addrs[DS18B20_MAX_DEVICES];

static TaskHandle_t              s_worker_task = NULL;
static TimerHandle_t             s_periodic_timer = NULL;
static uint32_t                  s_period_ms = 5000;

static volatile bool             s_continuous_mode = false;
static volatile bool             s_running = false;

static ds18b20_temp_cb_t         s_once_callback = NULL;
static void                     *s_once_user_ctx = NULL;

/* ========== 新增：温度缓存 + 互斥锁 ========== */
static float             s_cached_temps[DS18B20_MAX_DEVICES];
static bool              s_cached_valid[DS18B20_MAX_DEVICES];
static bool              s_cached_filled = false;   /* 是否至少写入过一次 */
static SemaphoreHandle_t s_cache_mutex = NULL;

/* ---------- CRC8 校验 ---------- */
static uint8_t crc8_ds18b20(const uint8_t *data, size_t len)
{
   uint8_t crc = 0;
   for (size_t i = 0; i < len; i++) {
      uint8_t byte = data[i];
      for (int b = 0; b < 8; b++) {
         uint8_t mix = (crc ^ byte) & 0x01;
         crc >>= 1;
         if (mix) crc ^= 0x8C;
         byte >>= 1;
      }
   }
   return crc;
}

/* ---------- 手动读取单个设备的暂存器并解析温度 ---------- */
static bool manual_read_one_device(int index, float *out_temp)
{
   if (onewire_bus_reset(s_bus) != ESP_OK) {
      DS18B20_LOGE("DS18B20[%d]: bus reset failed (no presence)", index);
      return false;
   }

   uint8_t match_cmd[9];
   match_cmd[0] = 0x55;
   memcpy(&match_cmd[1], &s_device_addrs[index], 8);
   if (onewire_bus_write_bytes(s_bus, match_cmd, 9) != ESP_OK) {
      DS18B20_LOGE("DS18B20[%d]: Match ROM failed", index);
      return false;
   }

   uint8_t read_cmd = 0xBE;
   if (onewire_bus_write_bytes(s_bus, &read_cmd, 1) != ESP_OK) {
      DS18B20_LOGE("DS18B20[%d]: Read Scratchpad cmd failed", index);
      return false;
   }

   uint8_t sp[9];
   if (onewire_bus_read_bytes(s_bus, sp, 9) != ESP_OK) {
      DS18B20_LOGE("DS18B20[%d]: read scratchpad failed", index);
      return false;
   }

   uint8_t crc = crc8_ds18b20(sp, 9);
   if (crc != 0) {
      DS18B20_LOGE("DS18B20[%d]: CRC error (remainder=0x%02X)", index, crc);
      return false;
   }

   int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
   *out_temp = raw / 16.0f;
   return true;
}

/* ---------- 内部：读取所有设备、更新缓存、通知回调 ---------- */
static void read_all_and_notify(ds18b20_temp_cb_t callback, void *user_ctx)
{
   for (int i = 0; i < s_device_count; i++) {
      float temp = 0.0f;
      bool ok = manual_read_one_device(i, &temp);

      /* ===== 更新缓存（互斥保护）===== */
      if (s_cache_mutex != NULL) {
         xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
         s_cached_temps[i] = temp;
         s_cached_valid[i] = ok;
         s_cached_filled = true;
         xSemaphoreGive(s_cache_mutex);
      }

      if (ok) {
         DS18B20_LOGI("DS18B20[%d]: %.2f °C", i, temp);
         if (callback) {
            callback(i, temp, user_ctx);
         }
      } else {
         DS18B20_LOGE("DS18B20[%d]: read failed", i);
         if (callback) {
            callback(i, -999.0f, user_ctx);
         }
      }
   }
}

/* ---------- 执行一次完整的"触发 → 等待 → 读取"循环 ---------- */
/* ---------- 执行一次完整的"触发 → 等待 → 读取"循环 ---------- */
static void do_one_cycle(void)
{
   if (onewire_bus_reset(s_bus) != ESP_OK) {
      DS18B20_LOGE("Worker: bus reset failed (no presence)");

      /* ← 总线故障：所有设备标记为无效 */
      if (s_cache_mutex != NULL) {
         xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
         for (int i = 0; i < s_device_count; i++) {
            s_cached_valid[i] = false;
         }
         xSemaphoreGive(s_cache_mutex);
      }
      return;
   }

   uint8_t conv_cmd[] = {0xCC, 0x44};
   if (onewire_bus_write_bytes(s_bus, conv_cmd, sizeof(conv_cmd)) != ESP_OK) {
      DS18B20_LOGE("Worker: Skip ROM + Convert T failed");

      /* ← Convert T 失败：所有设备标记为无效 */
      if (s_cache_mutex != NULL) {
         xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
         for (int i = 0; i < s_device_count; i++) {
            s_cached_valid[i] = false;
         }
         xSemaphoreGive(s_cache_mutex);
      }
      return;
   }

   vTaskDelay(pdMS_TO_TICKS(DS18B20_CONV_DELAY_MS));

   ds18b20_temp_cb_t cb = s_once_callback;
   void *ctx = s_once_user_ctx;
   s_once_callback = NULL;
   s_once_user_ctx = NULL;

   read_all_and_notify(cb, ctx);
}

/* ---------- 工作线程 ---------- */
static void ds18b20_worker_task(void *arg)
{
   DS18B20_LOGI("Worker task started, waiting for trigger...");

   while (1) {
      if (s_continuous_mode) {
         while (s_continuous_mode) {
            do_one_cycle();
         }
         DS18B20_LOGI("Continuous mode stopped, back to idle");
      }

      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      if (!s_continuous_mode) {
         do_one_cycle();
      }
   }
}

/* ---------- 周期定时器回调 ---------- */
static void periodic_timer_cb(TimerHandle_t timer)
{
   if (s_worker_task != NULL && !s_continuous_mode) {
      xTaskNotifyGive(s_worker_task);
   }
}

/* ===================================================================
 *  公开 API
 * =================================================================== */

void ds18b20_sensor_init(int gpio_num, int max_devices)
{
   if (max_devices > DS18B20_MAX_DEVICES) {
      max_devices = DS18B20_MAX_DEVICES;
   }

   /* ---- 创建缓存互斥锁 ---- */
   s_cache_mutex = xSemaphoreCreateMutex();
   memset(s_cached_temps, 0, sizeof(s_cached_temps));
   memset(s_cached_valid, 0, sizeof(s_cached_valid));
   s_cached_filled = false;

   /* ---- 1. 安装 1-Wire 总线 ---- */
   onewire_bus_config_t bus_config = {
      .bus_gpio_num = gpio_num,
      .flags = { .en_pull_up = false }
   };
   onewire_bus_rmt_config_t rmt_config = {
      .max_rx_bytes = 10,
   };
   ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &s_bus));
   DS18B20_LOGI("1-Wire bus installed on GPIO%d (RMT backend, external pull-up)", gpio_num);

   /* ---- 2. 搜索设备 ---- */
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
                  DS18B20_LOGI("Found DS18B20[%d], role: %s, resolution set to 11-bit",
                               s_device_count, s_device_roles[s_device_count]);
               } else {
                  DS18B20_LOGW("DS18B20[%d]: set 11-bit failed (err=%d)", s_device_count, res_err);
               }

               onewire_device_address_t addr;
               ds18b20_get_device_address(handle, &addr);
               DS18B20_LOGI("  ROM: %016llX", addr);
               s_device_addrs[s_device_count] = addr;
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

   /* ---- 3. 创建工作线程 ---- */
   xTaskCreate(
      ds18b20_worker_task,
      "ds18b20_worker",
      4096,
      NULL,
      3,
      &s_worker_task
   );

   /* ---- 4. 创建周期定时器 ---- */
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

/* ---- 直接总线读取（保留，但连续模式期间不要调用）---- */
bool ds18b20_sensor_get_temperature(int index, float *temperature)
{
   if (index < 0 || index >= s_device_count || temperature == NULL) {
      return false;
   }
   return manual_read_one_device(index, temperature);
}

/* ========== 新增：线程安全缓存读取 ========== */
bool ds18b20_sensor_get_temperature_cached(int index, float *temperature)
{
   if (index < 0 || index >= s_device_count || temperature == NULL) {
      return false;
   }
   if (s_cache_mutex == NULL || !s_cached_filled) {
      return false;
   }

   xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
   *temperature = s_cached_temps[index];
   bool valid = s_cached_valid[index];
   xSemaphoreGive(s_cache_mutex);

   return valid;
}

/* ---- 阻塞式全部读取（保留，仅用于离线调试）---- */
bool ds18b20_sensor_get_all_temperatures_blocking(float *temperatures)
{
   if (s_device_count == 0) {
      DS18B20_LOGW("No devices");
      return false;
   }

   if (onewire_bus_reset(s_bus) != ESP_OK) {
      DS18B20_LOGE("Trigger: bus reset failed");
      return false;
   }
   uint8_t conv_cmd[] = {0xCC, 0x44};
   if (onewire_bus_write_bytes(s_bus, conv_cmd, sizeof(conv_cmd)) != ESP_OK) {
      DS18B20_LOGE("Trigger: Skip ROM + Convert T failed");
      return false;
   }
   vTaskDelay(pdMS_TO_TICKS(DS18B20_CONV_DELAY_MS));
   bool all_ok = true;
   for (int i = 0; i < s_device_count; i++) {
      if (!manual_read_one_device(i, &temperatures[i])) {
         temperatures[i] = -999.0f;
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
}

void ds18b20_sensor_trigger_conversion(void)
{
   if (s_device_count == 0) return;
   if (onewire_bus_reset(s_bus) != ESP_OK) {
      DS18B20_LOGE("trigger_conversion: bus reset failed");
      return;
   }
   uint8_t conv_cmd[] = {0xCC, 0x44};
   if (onewire_bus_write_bytes(s_bus, conv_cmd, sizeof(conv_cmd)) != ESP_OK) {
      DS18B20_LOGE("trigger_conversion: Skip ROM + Convert T failed");
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

void ds18b20_sensor_start_reading(uint32_t interval_ms)
{
   if (s_device_count == 0) {
      DS18B20_LOGE("Cannot start: no DS18B20 devices");
      return;
   }
   s_continuous_mode = false;

   if (interval_ms < DS18B20_CONV_DELAY_MS + 20) {
      interval_ms = DS18B20_CONV_DELAY_MS + 20;
      DS18B20_LOGW("Interval too short, adjusted to %lu ms", (unsigned long)interval_ms);
   }

   s_period_ms = interval_ms;
   s_running = true;

   xTimerChangePeriod(s_periodic_timer, pdMS_TO_TICKS(s_period_ms), 0);
   xTimerStart(s_periodic_timer, 0);

   DS18B20_LOGI("Periodic reading started (interval: %lu ms)", (unsigned long)s_period_ms);
}

void ds18b20_sensor_start_continuous(void)
{
   if (s_device_count == 0) {
      DS18B20_LOGE("Cannot start continuous: no DS18B20 devices");
      return;
   }

   xTimerStop(s_periodic_timer, 0);

   s_running = true;
   s_continuous_mode = true;

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

void ds18b20_set_role(int index, const char *role) {
   if (index >= 0 && index < DS18B20_MAX_DEVICES) {
      strncpy(s_device_roles[index], role, sizeof(s_device_roles[index]) - 1);
   }
}
const char *ds18b20_get_role(int index) {
   if (index >= 0 && index < DS18B20_MAX_DEVICES) {
      return s_device_roles[index];
   }
   return "unknown";
}