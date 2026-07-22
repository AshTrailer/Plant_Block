#include "sht30_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "i2c_bus.h"
#include "sht3x.h"
#include "cloud_comm.h"

static const char *TAG = "SHT30_SENSOR";

// ---------- 日志宏 ----------
#define SHT30_LOGI(fmt, ...) do { \
    ESP_LOGI(TAG, fmt, ##__VA_ARGS__); \
} while(0)

#define SHT30_LOGE(fmt, ...) do { \
    ESP_LOGE(TAG, fmt, ##__VA_ARGS__); \
} while(0)

#define SHT30_LOGW(fmt, ...) do { \
    ESP_LOGW(TAG, fmt, ##__VA_ARGS__); \
} while(0)

// ---------- 配置 ----------
#define SHT30_READ_INTERVAL_MS   1000   // 1Hz
#define SHT30_I2C_FREQ_HZ        100000 // 100kHz

// ---------- 静态状态 ----------
static i2c_bus_handle_t   s_i2c_bus = NULL;
static sht3x_handle_t     s_sht3x = NULL;

static TaskHandle_t       s_worker_task = NULL;
static TimerHandle_t      s_timer = NULL;

static sht30_data_cb_t    s_callback = NULL;
static void              *s_user_ctx = NULL;

static volatile bool      s_running = false;

static float             s_last_temp = 0.0f;
static float             s_last_humidity = 0.0f;
static bool              s_data_valid = false;
static SemaphoreHandle_t s_data_mutex = NULL;
// ---------- 工作线程 ----------
// ========== 工作线程 ==========
static void sht30_worker_task(void *arg)
{
   SHT30_LOGI("Worker task started, waiting for trigger...");

   while (1) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      if (!s_running) {
         continue;
      }

      float temp = 0.0f;
      float hum = 0.0f;
      esp_err_t err = sht3x_get_humiture(s_sht3x, &temp, &hum);

      xSemaphoreTake(s_data_mutex, portMAX_DELAY);
      if (err == ESP_OK) {
         s_last_temp = temp;
         s_last_humidity = hum;
         s_data_valid = true;
      } else {
         /* ← 关键：失败时立即标记无效，避免假在线 */
         s_data_valid = false;
         SHT30_LOGE("Read failed (err=%d)", err);
      }
      xSemaphoreGive(s_data_mutex);

      if (err == ESP_OK) {
         SHT30_LOGI("Temperature: %.2f °C, Humidity: %.2f %%RH", temp, hum);
         if (s_callback) {
            s_callback(temp, hum, s_user_ctx);
         }
      }
   }
}

// ---------- 定时器回调（极轻量）----------
static void timer_cb(TimerHandle_t timer)
{
   if (s_worker_task != NULL && s_running) {
      xTaskNotifyGive(s_worker_task);
   }
}

// ===================================================================
//  公开 API
// ===================================================================

void sht30_sensor_init(int sda_pin, int scl_pin)
{
   // --- 1. 创建 I2C 总线 ---
   i2c_config_t i2c_conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = sda_pin,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_io_num = scl_pin,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = SHT30_I2C_FREQ_HZ,
   };

   s_i2c_bus = i2c_bus_create(I2C_NUM_0, &i2c_conf);
   if (s_i2c_bus == NULL) {
      SHT30_LOGE("Failed to create I2C bus");
      return;
   }
   SHT30_LOGI("I2C bus created: SDA=GPIO%d, SCL=GPIO%d", sda_pin, scl_pin);

   // --- 2. 创建 SHT3x 传感器实例 ---
   s_sht3x = sht3x_create(s_i2c_bus, SHT3x_ADDR_PIN_SELECT_VSS);
   if (s_sht3x == NULL) {
      SHT30_LOGE("Failed to create SHT3x sensor! Check wiring.");
      return;
   }

   // --- 3. 设为周期测量模式：1 次/秒，中等重复性 ---
   esp_err_t err = sht3x_set_measure_mode(s_sht3x, SHT3x_PER_1_MEDIUM);
   if (err != ESP_OK) {
      SHT30_LOGE("Failed to set periodic mode (err=%d)", err);
      return;
   }
   SHT30_LOGI("Sensor initialized, periodic mode: 1 measurement/sec, medium repeatability");

   s_data_mutex = xSemaphoreCreateMutex();
   if (s_data_mutex == NULL) {
      SHT30_LOGE("Failed to create mutex");
      return;
   }

   // --- 4. 创建工作线程 ---
   xTaskCreate(
      sht30_worker_task,
      "sht30_worker",
      3072,
      NULL,
      3,
      &s_worker_task
   );

   // --- 5. 创建 1Hz 定时器（不启动）---
   s_timer = xTimerCreate(
      "sht30_timer",
      pdMS_TO_TICKS(SHT30_READ_INTERVAL_MS),
      pdTRUE,    // 自动重载
      NULL,
      timer_cb
   );

   if (s_timer == NULL) {
      SHT30_LOGE("Failed to create timer");
      return;
   }

   SHT30_LOGI("SHT30 sensor module initialized");
}

void sht30_sensor_start(void)
{
   if (s_sht3x == NULL) {
      SHT30_LOGE("Cannot start: sensor not initialized");
      return;
   }

   s_running = true;

   // 等待传感器完成第一次测量（周期模式启动后首个测量约需 6ms）
   vTaskDelay(pdMS_TO_TICKS(20));

   xTimerStart(s_timer, 0);
   SHT30_LOGI("1Hz continuous reading started");
}

void sht30_sensor_stop(void)
{
   s_running = false;
   if (s_timer != NULL) {
      xTimerStop(s_timer, 0);
   }
   SHT30_LOGI("Reading stopped");
}

// ========== sht30_sensor_get_data 加锁 ==========
bool sht30_sensor_get_data(float *temperature, float *humidity)
{
   if (temperature == NULL || humidity == NULL) {
      return false;
   }
   if (s_data_mutex == NULL) {
      return false;
   }
   xSemaphoreTake(s_data_mutex, portMAX_DELAY);
   bool valid = s_data_valid;
   if (valid) {
      *temperature = s_last_temp;
      *humidity = s_last_humidity;
   }
   xSemaphoreGive(s_data_mutex);
   return valid;
}

void sht30_sensor_set_callback(sht30_data_cb_t callback, void *user_ctx)
{
   s_callback = callback;
   s_user_ctx = user_ctx;
}