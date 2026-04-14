#include "light_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cloud_comm.h"
#include "driver/ledc.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "LIGHT_CONTROL";
// 散热风扇状态变量
static bool s_fan_enabled = false;

#define LIGHT_LOGI(fmt, ...) do { \
   ESP_LOGI(TAG, fmt, ##__VA_ARGS__); \
   cloud_comm_publish_log("[I] " fmt, ##__VA_ARGS__); \
} while(0)

#define LIGHT_LOGE(fmt, ...) do { \
   ESP_LOGE(TAG, fmt, ##__VA_ARGS__); \
   cloud_comm_publish_log("[E] " fmt, ##__VA_ARGS__); \
} while(0)

#define LIGHT_LOGW(fmt, ...) do { \
   ESP_LOGW(TAG, fmt, ##__VA_ARGS__); \
   cloud_comm_publish_log("[W] " fmt, ##__VA_ARGS__); \
} while(0)

// PWM配置
#define PWM_TIMER          LEDC_TIMER_0
#define PWM_MODE           LEDC_LOW_SPEED_MODE
#define PWM_FREQUENCY_HZ   19000     // 19kHz
#define PWM_RESOLUTION     LEDC_TIMER_12_BIT
#define PWM_MAX_DUTY       4095      // 2^12 - 1
#define PWM_MIN_DUTY_PERCENT 2       // 最小占空比2%
#define PWM_MAX_DUTY_PERCENT 100     // 最大占空比100%

// 自动调光参数
#define RAMP_START_RATIO 0.3f        // 前30%时间渐亮
#define RAMP_END_RATIO   0.7f        // 后30%时间渐暗（从70%位置开始）
#define RAMP_MIN_PERCENT 2.0f        // 渐亮起始占空比2%

// 模块内部状态
typedef struct {
   int start_hour;      // 开启时间-小时 (0-23)
   int start_minute;    // 开启时间-分钟 (0-59)
   int end_hour;        // 关闭时间-小时 (0-23)
   int end_minute;      // 关闭时间-分钟 (0-59)
   float duration_hours; // 总照明时长（小时）
   int pwm_pins[LIGHT_CHANNEL_COUNT]; // 四通道PWM控制引脚
   int fan_pin;         // 散热风扇控制引脚
   bool is_manual_mode; // 是否为手动模式
   bool manual_state;   // 手动模式下的状态
} light_schedule_t;

static light_schedule_t s_schedule = {
   .start_hour = 8,     // 默认早上8点开启
   .start_minute = 0,
   .end_hour = 18,      // 默认晚上6点关闭
   .end_minute = 0,
   .duration_hours = 10.0, // 默认10小时
   .pwm_pins = {4, 16 ,18, 17}, // 默认四个PWM引脚
   .fan_pin = 15, // 默认散热风扇引脚
   .is_manual_mode = false,
   .manual_state = false
};

static light_state_t s_current_state = LIGHT_STATE_OFF;
static uint8_t s_current_pwm_duty = 0;  // 当前PWM占空比（0-100%）
static TickType_t s_last_update_ticks = 0;
static TickType_t s_last_pwm_update_ticks = 0;
static const TickType_t s_update_interval_ticks = 1000 / portTICK_PERIOD_MS; // 1秒
static const TickType_t s_pwm_update_interval_ticks = 1000 / portTICK_PERIOD_MS; // 1秒（PWM更新）
static bool s_pwm_initialized = false;

// 检查时间参数有效性
static bool check_time_params(int hour, int minute) {
   if (hour < 0 || hour > 23) {
      LIGHT_LOGE("小时超出范围: %d (应为0-23)", hour);
      return false;
   }
   if (minute < 0 || minute > 59) {
      LIGHT_LOGE("分钟超出范围: %d (应为0-59)", minute);
      return false;
   }
   return true;
}

// 控制散热风扇开关
static void control_fan(bool enable) {
   if (s_schedule.fan_pin < 0) {
      return; // 未配置风扇引脚
   }
   
   if (s_fan_enabled == enable) {
      return; // 状态未变化
   }
   
   // 设置GPIO输出电平
   // 假设高电平开启风扇，低电平关闭风扇
   gpio_set_level(s_schedule.fan_pin, enable ? 1 : 0);
   s_fan_enabled = enable;
   
   LIGHT_LOGI("散热风扇 %s", enable ? "开启" : "关闭");
}


// 将时间（小时+分钟）转换为分钟数（0-1439）
static int time_to_minutes(int hour, int minute) {
   return hour * 60 + minute;
}

// 检查当前时间是否在补光灯开启时段内
static bool should_light_be_on(void) {
   if (s_schedule.is_manual_mode) {
      return s_schedule.manual_state;
   }
   
   // 获取当前时间
   system_time_t current_time = time_manager_get_time();
   int current_minutes = time_to_minutes(current_time.hour, current_time.minute);
   int start_minutes = time_to_minutes(s_schedule.start_hour, s_schedule.start_minute);
   int end_minutes = time_to_minutes(s_schedule.end_hour, s_schedule.end_minute);
   
   // 处理跨午夜的情况（如23:00到01:00）
   if (end_minutes < start_minutes) {
      end_minutes += 24 * 60; // 结束时间加一天
      if (current_minutes < start_minutes) {
         current_minutes += 24 * 60; // 当前时间加一天
      }
   }
   
   // 检查是否在时间段内
   return (current_minutes >= start_minutes && current_minutes < end_minutes);
}

// 计算已照明时间（分钟）
static float get_elapsed_minutes_today(void) {
   system_time_t current_time = time_manager_get_time();
   int current_minutes = time_to_minutes(current_time.hour, current_time.minute);
   int start_minutes = time_to_minutes(s_schedule.start_hour, s_schedule.start_minute);
   
   if (current_minutes < start_minutes) {
      return 0; // 还没到开启时间
   }
   
   return (float)(current_minutes - start_minutes);
}

// 设置所有通道的PWM占空比（0-100%）
static bool set_all_channels_pwm_duty(uint8_t duty_percent) {
   if (!s_pwm_initialized) {
      LIGHT_LOGE("PWM未初始化");
      return false;
   }
   
   if (duty_percent > PWM_MAX_DUTY_PERCENT) {
      LIGHT_LOGE("占空比超出范围: %d (0-100)", duty_percent);
      return false;
   }
   
   // 计算实际占空比值
   uint32_t duty_value = 0;
   if (duty_percent > 0) {
      duty_value = (uint32_t)((float)duty_percent / 100.0f * (float)PWM_MAX_DUTY);
   }
   
   // 更新所有通道的占空比
   for (int i = 0; i < LIGHT_CHANNEL_COUNT; i++) {
      ledc_channel_t channel = (ledc_channel_t)i; // LEDC_CHANNEL_0 到 LEDC_CHANNEL_3
      
      esp_err_t ret = ledc_set_duty(PWM_MODE, channel, duty_value);
      if (ret != ESP_OK) {
         LIGHT_LOGE("设置通道%d PWM占空比失败: %s", i, esp_err_to_name(ret));
         return false;
      }
      
      // 更新输出
      ret = ledc_update_duty(PWM_MODE, channel);
      if (ret != ESP_OK) {
         LIGHT_LOGE("更新通道%d PWM输出失败: %s", i, esp_err_to_name(ret));
         return false;
      }
   }
   
   s_current_pwm_duty = duty_percent;
   
   // LIGHT_LOGI("所有通道PWM占空比设置为: %d%% (值: %d)", duty_percent, duty_value);
   return true;
}

// 计算自动调光所需的PWM占空比
static uint8_t calculate_auto_pwm_duty(void) {
   if (s_schedule.duration_hours <= 0) {
      return PWM_MAX_DUTY_PERCENT; // 默认100%
   }
   
   // 获取已照明时间（小时）
   float elapsed_minutes = get_elapsed_minutes_today();
   float elapsed_hours = elapsed_minutes / 60.0f;
   
   // 计算当前时间在总照明时长中的比例
   float time_ratio = elapsed_hours / s_schedule.duration_hours;
   
   // 如果不在照明时间段内，返回0
   if (time_ratio < 0 || time_ratio >= 1.0f) {
      return 0;
   }
   
   // 前30%时间内：从2%线性增加到100%
   if (time_ratio <= RAMP_START_RATIO) {
      // 线性插值：从RAMP_MIN_PERCENT到100%
      float ramp_ratio = time_ratio / RAMP_START_RATIO;
      float duty_percent = RAMP_MIN_PERCENT + ramp_ratio * (100.0f - RAMP_MIN_PERCENT);
      return (uint8_t)duty_percent;
   }
   
   // 中间40%时间内：保持100%
   if (time_ratio <= RAMP_END_RATIO) {
      return PWM_MAX_DUTY_PERCENT;
   }
   
   // 后30%时间内：从100%线性降低到2%
   float ramp_ratio = (time_ratio - RAMP_END_RATIO) / (1.0f - RAMP_END_RATIO);
   float duty_percent = 100.0f - ramp_ratio * (100.0f - RAMP_MIN_PERCENT);
   return (uint8_t)duty_percent;
}

// 初始化PWM（四通道）
static bool pwm_init(void) {
   if (s_pwm_initialized) {
      LIGHT_LOGW("PWM已经初始化");
      return true;
   }
   
   // 配置定时器（所有通道共享同一个定时器）
   ledc_timer_config_t timer_config = {
      .speed_mode       = PWM_MODE,
      .timer_num        = PWM_TIMER,
      .duty_resolution  = PWM_RESOLUTION,
      .freq_hz          = PWM_FREQUENCY_HZ,
      .clk_cfg          = LEDC_AUTO_CLK
   };
   
   esp_err_t ret = ledc_timer_config(&timer_config);
   if (ret != ESP_OK) {
      LIGHT_LOGE("PWM定时器配置失败: %s", esp_err_to_name(ret));
      return false;
   }
   
   // 配置四个通道
   for (int i = 0; i < LIGHT_CHANNEL_COUNT; i++) {
      ledc_channel_config_t channel_config = {
         .gpio_num       = s_schedule.pwm_pins[i],
         .speed_mode     = PWM_MODE,
         .channel        = (ledc_channel_t)i, // LEDC_CHANNEL_0 到 LEDC_CHANNEL_3
         .intr_type      = LEDC_INTR_DISABLE,
         .timer_sel      = PWM_TIMER,
         .duty           = 0,  // 初始占空比为0
         .hpoint         = 0
      };
      
      ret = ledc_channel_config(&channel_config);
      if (ret != ESP_OK) {
         LIGHT_LOGE("通道%d PWM配置失败: %s", i, esp_err_to_name(ret));
         return false;
      }
   }
   
   s_pwm_initialized = true;
   s_current_pwm_duty = 0;
   
   LIGHT_LOGI("四通道PWM初始化完成");
   for (int i = 0; i < LIGHT_CHANNEL_COUNT; i++) {
      LIGHT_LOGI("通道%d PWM引脚: GPIO%d", i, s_schedule.pwm_pins[i]);
   }
   LIGHT_LOGI("频率: %d Hz", PWM_FREQUENCY_HZ);
   LIGHT_LOGI("分辨率: 12位 (0-%d)", PWM_MAX_DUTY);
   LIGHT_LOGI("最小占空比: %d%%", PWM_MIN_DUTY_PERCENT);
   
   return true;
}

// 初始化散热风扇GPIO
static bool fan_init(void) {
   if (s_schedule.fan_pin < 0) {
      LIGHT_LOGW("未配置散热风扇引脚");
      return false;
   }
   
   // 配置GPIO为输出模式
   gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << s_schedule.fan_pin),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
   };
   
   esp_err_t ret = gpio_config(&io_conf);
   if (ret != ESP_OK) {
      LIGHT_LOGE("散热风扇GPIO配置失败: %s", esp_err_to_name(ret));
      return false;
   }
   
   // 初始状态为关闭
   gpio_set_level(s_schedule.fan_pin, 0);
   s_fan_enabled = false;
   
   LIGHT_LOGI("散热风扇初始化完成，引脚: GPIO%d", s_schedule.fan_pin);
   return true;
}

// 初始化补光灯控制模块（四通道）
void light_control_init(const int pwm_pins[LIGHT_CHANNEL_COUNT], int fan_pin) {
   // 复制引脚配置
   for (int i = 0; i < LIGHT_CHANNEL_COUNT; i++) {
      s_schedule.pwm_pins[i] = pwm_pins[i];
   }  
   s_schedule.fan_pin = fan_pin;
   
   // 初始化PWM
   pwm_init();
   
   // 初始化散热风扇
   fan_init();
   
   LIGHT_LOGI("四通道补光灯控制模块初始化完成");
   for (int i = 0; i < LIGHT_CHANNEL_COUNT; i++) {
      LIGHT_LOGI("通道%d PWM调光引脚: GPIO%d", i, pwm_pins[i]);
   }
   LIGHT_LOGI("散热风扇控制引脚: GPIO%d", fan_pin);
   LIGHT_LOGI("默认计划: %02d:%02d - %02d:%02d (%.1f小时)", 
            s_schedule.start_hour, s_schedule.start_minute,
            s_schedule.end_hour, s_schedule.end_minute,
            s_schedule.duration_hours);
}

// 设置补光灯的每日开启时间
bool light_control_set_start_time(int hour, int minute) {
   if (!check_time_params(hour, minute)) {
      return false;
   }
   
   s_schedule.start_hour = hour;
   s_schedule.start_minute = minute;
   
   // 自动计算照明时长
   int start_minutes = time_to_minutes(s_schedule.start_hour, s_schedule.start_minute);
   int end_minutes = time_to_minutes(s_schedule.end_hour, s_schedule.end_minute);
   
   if (end_minutes < start_minutes) {
      end_minutes += 24 * 60;
   }
   
   s_schedule.duration_hours = (float)(end_minutes - start_minutes) / 60.0f;
   
   LIGHT_LOGI("开启时间已设置为: %02d:%02d", hour, minute);
   LIGHT_LOGI("自动计算照明时长: %.1f小时", s_schedule.duration_hours);
   return true;
}

// 设置补光灯的每日关闭时间
bool light_control_set_end_time(int hour, int minute) {
   if (!check_time_params(hour, minute)) {
      return false;
   }
   
   s_schedule.end_hour = hour;
   s_schedule.end_minute = minute;
   
   // 自动计算照明时长
   int start_minutes = time_to_minutes(s_schedule.start_hour, s_schedule.start_minute);
   int end_minutes = time_to_minutes(hour, minute);
   
   if (end_minutes < start_minutes) {
      end_minutes += 24 * 60;
   }
   
   s_schedule.duration_hours = (float)(end_minutes - start_minutes) / 60.0f;
   
   LIGHT_LOGI("关闭时间已设置为: %02d:%02d", hour, minute);
   LIGHT_LOGI("自动计算照明时长: %.1f小时", s_schedule.duration_hours);
   return true;
}

// 设置补光灯的总照明时长
bool light_control_set_duration(float hours) {
   if (hours <= 0 || hours > 24) {
      LIGHT_LOGE("照明时长超出范围: %.1f (应为0-24小时)", hours);
      return false;
   }
   
   s_schedule.duration_hours = hours;
   
   // 根据开始时间和时长计算结束时间
   int start_minutes = time_to_minutes(s_schedule.start_hour, s_schedule.start_minute);
   int duration_minutes = (int)(hours * 60);
   int end_minutes = start_minutes + duration_minutes;
   
   // 处理跨天
   if (end_minutes >= 24 * 60) {
      end_minutes -= 24 * 60;
   }
   
   s_schedule.end_hour = end_minutes / 60;
   s_schedule.end_minute = end_minutes % 60;
   
   LIGHT_LOGI("照明时长已设置为: %.1f小时", hours);
   LIGHT_LOGI("自动调整关闭时间: %02d:%02d", 
            s_schedule.end_hour, s_schedule.end_minute);
   return true;
}

// 更新补光灯状态
void light_control_update(void) {
   bool should_be_on = should_light_be_on();
   
   if (should_be_on) {
      // 计算自动调光所需的PWM占空比
      uint8_t target_duty = calculate_auto_pwm_duty();
      
      if (target_duty > 0) {
         if (s_current_state != LIGHT_STATE_PWM || s_current_pwm_duty != target_duty) {
            if (set_all_channels_pwm_duty(target_duty)) {
               s_current_state = LIGHT_STATE_PWM;
               // 开启散热风扇
               control_fan(true);
               LIGHT_LOGI("补光灯开启，PWM调光: %d%%，散热风扇开启", target_duty);
            }
         }
      } else {
         // 如果计算出的占空比为0，则关闭
         if (s_current_state != LIGHT_STATE_OFF) {
            if (set_all_channels_pwm_duty(0)) {
               s_current_state = LIGHT_STATE_OFF;
               // 关闭散热风扇
               control_fan(false);
               LIGHT_LOGI("补光灯关闭，散热风扇关闭");
            }
         }
      }
   } else {
      // 应该关闭
      if (s_current_state != LIGHT_STATE_OFF) {
         if (set_all_channels_pwm_duty(0)) {
            s_current_state = LIGHT_STATE_OFF;
            // 关闭散热风扇
            control_fan(false);
            LIGHT_LOGI("补光灯关闭，散热风扇关闭");
         }
      }
   }
}

// 更新PWM占空比（独立于开关状态更新）
static void update_pwm_duty(void) {
   if (s_schedule.is_manual_mode) {
      // 手动模式下不更新PWM
      return;
   }
   
   // 如果灯是开启状态（自动模式下），更新PWM占空比
   bool should_be_on = should_light_be_on();
   if (should_be_on && s_pwm_initialized) {
      uint8_t target_duty = calculate_auto_pwm_duty();
      if (target_duty != s_current_pwm_duty) {
         set_all_channels_pwm_duty(target_duty);
         LIGHT_LOGI("更新光照PWM: %d%%", target_duty);
      }
   }
}

// 获取当前补光灯状态
light_state_t light_control_get_state(void) {
    return s_current_state;
}

// 获取补光灯开关状态
bool light_control_is_on(void) {
    if (s_schedule.is_manual_mode) {
        return s_schedule.manual_state;
    }
    return (s_current_state != LIGHT_STATE_OFF);
}

// 直接控制补光灯开关（手动模式）
void light_control_manual_set(bool on) {
   s_schedule.is_manual_mode = true;
   s_schedule.manual_state = on;
   
   if (on) {
      s_current_state = LIGHT_STATE_ON;
      // 手动模式下，所有通道PWM设置为100%
      if (set_all_channels_pwm_duty(100)) {
         // 开启散热风扇
         control_fan(true);
         LIGHT_LOGI("补光灯手动开启，所有通道PWM: 100%%，散热风扇开启");
      }
   } else {
      s_current_state = LIGHT_STATE_OFF;
      // 关闭时，所有通道PWM设置为0
      if (set_all_channels_pwm_duty(0)) {
         // 关闭散热风扇
         control_fan(false);
         LIGHT_LOGI("补光灯手动关闭，散热风扇关闭");
      }
   }
}

// 轮询函数
void light_control_poll(void) {
   TickType_t current_ticks = xTaskGetTickCount();

   // 检查是否到达PWM更新间隔（1秒）
   if ((current_ticks - s_last_pwm_update_ticks) >= s_pwm_update_interval_ticks) {
      update_pwm_duty();  // 调用PWM更新函数
      s_last_pwm_update_ticks = current_ticks;
   }

   // 检查是否到达开关状态更新间隔（1秒）
   if ((current_ticks - s_last_update_ticks) >= s_update_interval_ticks) {
      light_control_update();  // 调用更新函数
      s_last_update_ticks = current_ticks;
   }
}

// 获取当前是否为手动模式
bool light_control_is_manual_mode(void) {
   return s_schedule.is_manual_mode;
}

// 设置自动模式
void light_control_set_auto_mode(void) {
   s_schedule.is_manual_mode = false;
   LIGHT_LOGI("补光灯切换为自动模式");

   // 切换到自动模式时，根据当前时间调整状态
   light_control_update();
}

// 获取开启时间 - 小时
int light_control_get_start_hour(void) {
   return s_schedule.start_hour;
}

// 获取开启时间 - 分钟
int light_control_get_start_minute(void) {
   return s_schedule.start_minute;
}

// 获取关闭时间 - 小时
int light_control_get_end_hour(void) {
   return s_schedule.end_hour;
}

// 获取关闭时间 - 分钟
int light_control_get_end_minute(void) {
   return s_schedule.end_minute;
}

// 获取照明时长
float light_control_get_duration(void) {
   return s_schedule.duration_hours;
}

// 获取当前PWM占空比
uint8_t light_control_get_pwm_duty(void) {
   return s_current_pwm_duty;
}

// 获取PWM控制引脚号
int light_control_get_pwm_pin(int channel) {
   if (channel < 0 || channel >= LIGHT_CHANNEL_COUNT) {
      LIGHT_LOGE("通道号无效: %d (应为0-%d)", channel, LIGHT_CHANNEL_COUNT - 1);
      return -1;
   }
   return s_schedule.pwm_pins[channel];
}

// 获取散热风扇引脚的函数
int light_control_get_fan_pin(void) {
   return s_schedule.fan_pin;
}

// 获取散热风扇状态的函数
bool light_control_is_fan_on(void) {
   return s_fan_enabled;
}
