#include "irrigation_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time_manager.h"
#include "gpio_control.h"
#include "moisture_sensor.h"
#include "float_switch.h"
#include "data_processor.h"
#include "cloud_comm.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "vofa_output.h"

static const char *TAG = "IRRIGATION";

#define IRR_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define IRR_LOGE(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#define IRR_LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)

// 蠕动泵参数：31.5s = 40ml → 0.7875 s/ml
#define ML_TO_MS(ml)  ((uint32_t)((float)(ml) * 787.5f))  // 转换为毫秒

// 模块配置
typedef struct {
   int pump_pin;
   int trigger_threshold;    // 湿度阈值（0-100%）
   int water_volume_ml;      // 单次浇水体积（ml，0-200）
   int week_min_times;
   int week_max_times;
} irrigation_config_t;

// 模块状态
typedef struct {
   bool is_watering;
   bool sensor_power;
   int  current_week;
   int  week_water_count;
   time_t last_water_time;
   time_t last_check_time;
   bool test_mode;
   bool time_reset_needed;
   bool force_check_needed;
} irrigation_state_t;

static irrigation_config_t s_config = {
   .pump_pin          = 18,
   .trigger_threshold = 50,
   .water_volume_ml   = 10,       // 默认 10ml
   .week_min_times    = 1,
   .week_max_times    = 5,
};

static irrigation_state_t s_state = {
   .is_watering       = false,
   .sensor_power      = false,
   .current_week      = 0,
   .week_water_count  = 0,
   .last_water_time   = 0,
   .last_check_time   = 0,
   .test_mode         = false,
   .time_reset_needed = false,
   .force_check_needed = false,
};

static TickType_t s_last_poll_ticks = 0;
static const TickType_t s_poll_interval_ticks = 1000 / portTICK_PERIOD_MS;

// ---- 前向声明 ----
static void start_watering(void);
static void stop_sensor_power(void);
static void start_sensor_power(void);

// ==================== 浮球开关检查 ====================
static bool water_tank_has_water(void)
{
   return float_switch_get_state();
}

// ==================== 浇水任务（带浮球监测）====================
static void watering_task(void *arg)
{
   uint32_t duration_ms = (uint32_t)(uintptr_t)arg;
   uint32_t elapsed = 0;
   const uint32_t check_ms = 500;

   while (elapsed < duration_ms) {
      vTaskDelay(pdMS_TO_TICKS(check_ms));
      elapsed += check_ms;

      if (!water_tank_has_water()) {
         IRR_LOGE("浇水过程中水箱缺水，立即停止！");
         gpio_control_set_level(s_config.pump_pin, false);
         s_state.is_watering = false;
         vTaskDelete(NULL);
         return;
      }
   }

   gpio_control_set_level(s_config.pump_pin, false);
   s_state.is_watering = false;

   //IRR_LOGI("浇水完成 (%lu ms)，本周 %d/%d 次",
   //         duration_ms, s_state.week_water_count, s_config.week_max_times);
   vofa_output_send("irrigation", "water_done,dur_ms=%lu,week=%d/%d",
                    duration_ms, s_state.week_water_count, s_config.week_max_times);
   vTaskDelete(NULL);
}

// ==================== 补浇水任务 ====================
static void makeup_watering_task(void *arg)
{
   uint32_t total_ms = (uint32_t)(uintptr_t)arg;
   uint32_t elapsed = 0;
   const uint32_t check_ms = 500;

   while (elapsed < total_ms) {
      vTaskDelay(pdMS_TO_TICKS(check_ms));
      elapsed += check_ms;

      if (!water_tank_has_water()) {
         IRR_LOGE("补浇水过程中水箱缺水，立即停止！");
         gpio_control_set_level(s_config.pump_pin, false);
         vTaskDelete(NULL);
         return;
      }
   }

   gpio_control_set_level(s_config.pump_pin, false);
   //IRR_LOGI("补浇水完成 (%lu ms)", total_ms);
   vofa_output_send("irrigation", "makeup_done,total_ms=%lu", total_ms);
   vTaskDelete(NULL);
}

static bool should_water_now(int moisture_value) {
   if (moisture_value >= s_config.trigger_threshold) {
      //IRR_LOGI("湿度 %d%% ≥ 阈值 %d%%，不浇水",
      //         moisture_value, s_config.trigger_threshold);
      vofa_output_send("irrigation", "skip,moisture=%d,reason=above_threshold", moisture_value);
      return false;
   }

   if (!s_state.test_mode) {
      if (s_state.last_water_time > 0) {
         time_t now = time_manager_get_unix_time();
         double hours = difftime(now, s_state.last_water_time) / 3600.0;
         if (hours < 4.0) {
            //IRR_LOGI("距上次浇水 %.1f h，不足 4h，不浇水", hours);
            vofa_output_send("irrigation", "skip,moisture=%d,reason=cooldown,last_h=%.1f",
                    moisture_value, hours);
            return false;
         }
      }
   }

   if (s_state.week_water_count >= s_config.week_max_times) {
      //IRR_LOGI("本周已浇 %d 次，达上限 %d，不浇水",
      //         s_state.week_water_count, s_config.week_max_times);
      vofa_output_send("irrigation", "skip,moisture=%d,reason=week_max,count=%d",
                    moisture_value, s_state.week_water_count);
      return false;
   }

   //IRR_LOGI("触发浇水: 湿度 %d%% < 阈值 %d%%", moisture_value, s_config.trigger_threshold);
   vofa_output_send("irrigation", "trigger,moisture=%d,threshold=%d",
                    moisture_value, s_config.trigger_threshold);
   return true;
}

// ==================== 传感器检查任务 ====================
static void sensor_check_task(void *arg)
{
   // 先检查水箱
   if (!water_tank_has_water()) {
      IRR_LOGE("水箱缺水，跳过浇水检查");
      stop_sensor_power();
      vTaskDelete(NULL);
      return;
   }

   float humidity = 0.0f;

   // 使用 moisture_sensor 的稳定读取（内部处理上电/稳定/采样/断电）
   if (!moisture_sensor_read_stable(&humidity)) {
      IRR_LOGE("湿度采样不稳定，放弃本次判断");
      stop_sensor_power();
      vTaskDelete(NULL);
      return;
   }

   // 采样完毕后再查一次水箱（采样耗时约 35s，期间水位可能变化）
   if (!water_tank_has_water()) {
      IRR_LOGE("采样期间水箱缺水，放弃浇水");
      stop_sensor_power();
      vTaskDelete(NULL);
      return;
   }

   if (should_water_now((int)humidity)) {
      start_watering();
   } else {
      //IRR_LOGI("湿度 %.1f%% ≥ 阈值 %d%%，无需浇水",
      //         humidity, s_config.trigger_threshold);
   }

   stop_sensor_power();
   vTaskDelete(NULL);
}

// ==================== 辅助函数 ====================
static bool is_new_week(void) {
   system_time_t current_time = time_manager_get_time();
   int current_weekday = time_manager_get_weekday();

   if (current_weekday == 1 &&
       current_time.hour == 0 &&
       current_time.minute == 0 &&
       current_time.second == 0) {
      int iso_week = time_manager_get_current_iso_week();
      if (iso_week != s_state.current_week) {
         s_state.current_week = iso_week;
         return true;
      }
   }
   return false;
}

static void start_sensor_power(void) {
   moisture_sensor_power_on();
   s_state.sensor_power = true;
}

static void stop_sensor_power(void) {
   moisture_sensor_power_off();
   s_state.sensor_power = false;
}

static void check_time_reset(void) {
   if (!s_state.time_reset_needed) return;
   time_t now = time_manager_get_unix_time();

   s_state.current_week = time_manager_get_current_iso_week();
   s_state.week_water_count = 0;
   //IRR_LOGI("时间重置：周数→%d，浇水计数清零", s_state.current_week);
   vofa_output_send("irrigation", "time_reset,iso_week=%d", s_state.current_week);

   if (s_state.last_water_time > 0 && difftime(s_state.last_water_time, now) > 0) {
      s_state.last_water_time = 0;
   }
   if (s_state.last_check_time > 0 && difftime(s_state.last_check_time, now) > 0) {
      s_state.last_check_time = now;
   }
   s_state.force_check_needed = true;
   s_state.time_reset_needed = false;
}

static void start_watering(void) {
   if (s_state.is_watering) {
      IRR_LOGW("已在浇水");
      
      return;
   }
   if (!water_tank_has_water()) {
      IRR_LOGE("水箱缺水，无法开始浇水");
      return;
   }

   s_state.is_watering = true;
   s_state.week_water_count++;
   s_state.last_water_time = time_manager_get_unix_time();

   uint32_t duration_ms = ML_TO_MS(s_config.water_volume_ml);
   gpio_control_set_level(s_config.pump_pin, true);
   //IRR_LOGI("开始浇水: %d ml → %lu ms", s_config.water_volume_ml, (unsigned long)duration_ms);
   vofa_output_send("irrigation", "water_start,vol=%d,dur_ms=%lu",
                    s_config.water_volume_ml, (unsigned long)duration_ms);

   xTaskCreate(watering_task, "watering_task", 3072,
               (void*)(uintptr_t)duration_ms, 2, NULL);
}

static void handle_week_minimum_watering(void) {
   if (!is_new_week()) return;
   int last_count = s_state.week_water_count;
   int required = s_config.week_min_times;
   //IRR_LOGI("新一周开始，ISO 周数: %d", s_state.current_week);
   vofa_output_send("irrigation", "week_reset,iso_week=%d,last_count=%d",
                    s_state.current_week, last_count);
   
   if (last_count < required) {
      int deficit = required - last_count;
      uint32_t total_ms = ML_TO_MS(s_config.water_volume_ml) * deficit;
      //IRR_LOGI("上周仅 %d 次，需补浇 %d 次，总 %lu ms",
      //         last_count, deficit, (unsigned long)total_ms);
      vofa_output_send("irrigation", "makeup_start,deficit=%d,total_ms=%lu",
                    deficit, (unsigned long)total_ms);

      s_state.week_water_count = 0;
      if (water_tank_has_water()) {
         gpio_control_set_level(s_config.pump_pin, true);
         xTaskCreate(makeup_watering_task, "makeup_watering", 3072,
                     (void*)(uintptr_t)total_ms, 2, NULL);
      } else {
         IRR_LOGE("水箱缺水，无法补浇");
      }
   } else {
      //IRR_LOGI("上周 %d 次 ≥ 最小 %d，无需补浇", last_count, required);
      //s_state.week_water_count = 0;
      vofa_output_send("irrigation", "makeup_skip,last=%d,min=%d", last_count, required);
   }
}

static void check_watering_schedule(void) {
   time_t now = time_manager_get_unix_time();
   if (difftime(now, s_state.last_check_time) < 14400.0) return;
   s_state.last_check_time = now;

   //IRR_LOGI("4h 检查点，准备检测土壤湿度");

   start_sensor_power();  // moisture_sensor_read_stable 内部会等待稳定

   xTaskCreate(sensor_check_task, "sensor_check", 4096, NULL, 2, NULL);
}

// ==================== 公开 API ====================

void irrigation_controller_init(int pump_pin) {
   s_config.pump_pin = pump_pin;
   s_state.current_week = time_manager_get_current_iso_week();
   s_state.last_check_time = time_manager_get_unix_time();

   IRR_LOGI("灌溉模块初始化: 泵 GPIO%d, 阈值 %d%%, 水量 %d ml, "
            "周 %d~%d 次, ISO周 %d",
            pump_pin, s_config.trigger_threshold, s_config.water_volume_ml,
            s_config.week_min_times, s_config.week_max_times,
            s_state.current_week);
}

void irrigation_controller_poll(void) {
   TickType_t now = xTaskGetTickCount();
   if ((now - s_last_poll_ticks) < s_poll_interval_ticks) return;

   check_time_reset();

   if (!s_state.test_mode) {
      handle_week_minimum_watering();

      if (s_state.force_check_needed) {
         //IRR_LOGI("强制检查（时间重置后）");
         if (water_tank_has_water()) {
            start_sensor_power();
            xTaskCreate(sensor_check_task, "sensor_check_f", 4096, NULL, 2, NULL);
         }
         s_state.force_check_needed = false;
         s_state.last_check_time = time_manager_get_unix_time();
      } else {
         check_watering_schedule();
      }
   }

   s_last_poll_ticks = now;
}

// ---- 设置/获取 ----

bool irrigation_controller_set_threshold(int threshold) {
   if (threshold < 0 || threshold > 100) { IRR_LOGE("阈值 0-100"); return false; }
   s_config.trigger_threshold = threshold;
   IRR_LOGI("阈值 → %d%%", threshold);
   return true;
}

bool irrigation_controller_set_volume(int volume_ml) {
   if (volume_ml < 0 || volume_ml > 200) { IRR_LOGE("体积 0-200 ml"); return false; }
   s_config.water_volume_ml = volume_ml;
   IRR_LOGI("单次水量 → %d ml (%.1f s)", volume_ml, (float)volume_ml * 0.7875f);
   return true;
}

bool irrigation_controller_set_week_min(int min_times) {
   if (min_times < 0) return false;
   s_config.week_min_times = min_times;
   IRR_LOGI("周最小 → %d", min_times);
   return true;
}

bool irrigation_controller_set_week_max(int max_times) {
   if (max_times < 0) return false;
   s_config.week_max_times = max_times;
   IRR_LOGI("周最大 → %d", max_times);
   return true;
}

int irrigation_controller_get_threshold(void) { return s_config.trigger_threshold; }
int irrigation_controller_get_volume(void)    { return s_config.water_volume_ml; }
int irrigation_controller_get_week_min(void)  { return s_config.week_min_times; }
int irrigation_controller_get_week_max(void)  { return s_config.week_max_times; }
int irrigation_controller_get_week_count(void){ return s_state.week_water_count; }
bool irrigation_controller_get_sensor_power_status(void) { return s_state.sensor_power; }

bool irrigation_controller_reset_week(void) {
   s_state.week_water_count = 0;
   IRR_LOGI("周计数已清零");
   return true;
}

void irrigation_controller_notify_time_reset(void) {
   s_state.time_reset_needed = true;
   IRR_LOGI("收到时间重置通知");
}