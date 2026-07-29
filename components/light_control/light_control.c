#include "light_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "LIGHT_CTRL";

#define LIGHT_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LIGHT_LOGE(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#define LIGHT_LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)

// PWM 硬件配置
#define PWM_TIMER          LEDC_TIMER_0
#define PWM_MODE           LEDC_LOW_SPEED_MODE
#define PWM_FREQUENCY_HZ   19000
#define PWM_RESOLUTION     LEDC_TIMER_12_BIT
#define PWM_MAX_DUTY       4095
#define PWM_MAX_PERCENT    95      // 硬件限制最大 95%
#define PWM_MIN_PERCENT    0

// 默认渐变比例（百分比，0-50）
#define DEFAULT_RAMP_UP    30
#define DEFAULT_RAMP_DOWN  30

// 内部状态
typedef struct {
   int  start_hour;
   int  start_minute;
   int  end_hour;
   int  end_minute;
   float duration_hours;
   int  pwm_pins[LIGHT_CHANNEL_COUNT];
   int  power_pin;            // 电源使能引脚（-1 = 无）
   int  ramp_up_ratio;        // 上升比例（%）
   int  ramp_down_ratio;      // 下降比例（%）
   bool is_manual_mode;
   bool manual_state;
} light_schedule_t;

static light_schedule_t s_sched = {
   .start_hour     = 8,
   .start_minute   = 0,
   .end_hour       = 18,
   .end_minute     = 0,
   .duration_hours = 10.0f,
   .pwm_pins       = {14},
   .power_pin      = -1,
   .ramp_up_ratio  = DEFAULT_RAMP_UP,
   .ramp_down_ratio= DEFAULT_RAMP_DOWN,
   .is_manual_mode = false,
   .manual_state   = false,
};

static light_state_t s_state = LIGHT_STATE_OFF;
static uint8_t       s_duty  = 0;
static bool          s_pwm_ok = false;

static TickType_t s_last_tick = 0;
static const TickType_t s_interval = 1000 / portTICK_PERIOD_MS;

// ==================== 硬件 ====================
static void power_pin_set(bool on)
{
   if (s_sched.power_pin < 0) return;
   gpio_set_level(s_sched.power_pin, on ? 1 : 0);
}

static void power_pin_init(void)
{
   if (s_sched.power_pin < 0) return;
   gpio_config_t c = {
      .pin_bit_mask = (1ULL << s_sched.power_pin),
      .mode         = GPIO_MODE_OUTPUT,
      .pull_up_en   = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type    = GPIO_INTR_DISABLE,
   };
   gpio_config(&c);
   gpio_set_level(s_sched.power_pin, 0);
   LIGHT_LOGI("电源引脚 GPIO%d 初始化", s_sched.power_pin);
}

static bool pwm_init(void)
{
   ledc_timer_config_t t = {
      .speed_mode      = PWM_MODE,
      .timer_num       = PWM_TIMER,
      .duty_resolution = PWM_RESOLUTION,
      .freq_hz         = PWM_FREQUENCY_HZ,
      .clk_cfg         = LEDC_AUTO_CLK,
   };
   if (ledc_timer_config(&t) != ESP_OK) return false;

   ledc_channel_config_t ch = {
      .gpio_num   = s_sched.pwm_pins[0],
      .speed_mode = PWM_MODE,
      .channel    = LEDC_CHANNEL_0,
      .intr_type  = LEDC_INTR_DISABLE,
      .timer_sel  = PWM_TIMER,
      .duty       = 0,
      .hpoint     = 0,
   };
   if (ledc_channel_config(&ch) != ESP_OK) return false;

   s_pwm_ok = true;
   LIGHT_LOGI("PWM 初始化: GPIO%d, %dHz", s_sched.pwm_pins[0], PWM_FREQUENCY_HZ);
   return true;
}

static bool set_pwm_duty(uint8_t pct)
{
   if (!s_pwm_ok) return false;
   if (pct > PWM_MAX_PERCENT) pct = PWM_MAX_PERCENT;
   uint32_t val = (uint32_t)((float)pct / 100.0f * PWM_MAX_DUTY);
   ledc_set_duty(PWM_MODE, LEDC_CHANNEL_0, val);
   ledc_update_duty(PWM_MODE, LEDC_CHANNEL_0);
   s_duty = pct;
   return true;
}

// ==================== 时间计算 ====================
static int time_to_mins(int h, int m) { return h * 60 + m; }

static bool in_schedule(void)
{
   if (s_sched.is_manual_mode) return s_sched.manual_state;

   system_time_t now = time_manager_get_time();
   int cur = time_to_mins(now.hour, now.minute);
   int start = time_to_mins(s_sched.start_hour, s_sched.start_minute);
   int end   = time_to_mins(s_sched.end_hour, s_sched.end_minute);
   if (end < start) { end += 1440; if (cur < start) cur += 1440; }
   return (cur >= start && cur < end);
}

static float elapsed_hours(void)
{
   system_time_t now = time_manager_get_time();
   int cur = time_to_mins(now.hour, now.minute);
   int start = time_to_mins(s_sched.start_hour, s_sched.start_minute);
   if (cur < start) return 0;
   return (float)(cur - start) / 60.0f;
}

// ==================== 渐变计算 ====================
static uint8_t calc_ramp_duty(void)
{
   if (s_sched.duration_hours <= 0) return PWM_MAX_PERCENT;

   float elapsed = elapsed_hours();
   float ratio = elapsed / s_sched.duration_hours;
   if (ratio < 0.0f || ratio >= 1.0f) return 0;

   float up_r   = s_sched.ramp_up_ratio / 100.0f;
   float down_r = s_sched.ramp_down_ratio / 100.0f;

   // 上升段
   if (ratio <= up_r) {
      float p = ratio / up_r;
      return (uint8_t)(p * PWM_MAX_PERCENT);
   }

   // 保持段
   float hold_end = 1.0f - down_r;
   if (ratio <= hold_end) {
      return PWM_MAX_PERCENT;
   }

   // 下降段
   float p = (ratio - hold_end) / down_r;
   return (uint8_t)((1.0f - p) * PWM_MAX_PERCENT);
}

// ==================== 公开 API ====================

void light_control_init(const int pwm_pins[LIGHT_CHANNEL_COUNT], int power_pin)
{
   for (int i = 0; i < LIGHT_CHANNEL_COUNT; i++)
      s_sched.pwm_pins[i] = pwm_pins[i];
   s_sched.power_pin = power_pin;
   power_pin_init();
   pwm_init();
   LIGHT_LOGI("COB LED 初始化: PWM=%d, Power=%d, %02d:%02d-%02d:%02d (%.1fh)",
              pwm_pins[0], power_pin,
              s_sched.start_hour, s_sched.start_minute,
              s_sched.end_hour, s_sched.end_minute,
              s_sched.duration_hours);
}

void light_control_set_ramp_up_ratio(int percent)
{
   if (percent < 0) percent = 0;
   if (percent > 50) percent = 50;
   s_sched.ramp_up_ratio = percent;
   LIGHT_LOGI("上升比例 → %d%%", percent);
}

void light_control_set_ramp_down_ratio(int percent)
{
   if (percent < 0) percent = 0;
   if (percent > 50) percent = 50;
   s_sched.ramp_down_ratio = percent;
   LIGHT_LOGI("下降比例 → %d%%", percent);
}

void light_control_manual_set(bool on)
{
   s_sched.is_manual_mode = true;
   s_sched.manual_state = on;

   if (on) {
      s_state = LIGHT_STATE_ON;
      set_pwm_duty(PWM_MAX_PERCENT);   // 95%
      power_pin_set(true);
      LIGHT_LOGI("手动开: PWM=%d%% 电源=ON", PWM_MAX_PERCENT);
   } else {
      s_state = LIGHT_STATE_OFF;
      set_pwm_duty(0);
      power_pin_set(false);
      LIGHT_LOGI("手动关: PWM=0%% 电源=OFF");
   }
}

void light_control_set_auto_mode(void)
{
   s_sched.is_manual_mode = false;
   LIGHT_LOGI("切换为自动模式");
   light_control_update();
}

void light_control_poll(void)
{
   TickType_t now = xTaskGetTickCount();
   if ((now - s_last_tick) < s_interval) return;
   s_last_tick = now;

   light_control_update();
}

void light_control_update(void)
{
   if (s_sched.is_manual_mode) return;

   bool should = in_schedule();
   if (!should) {
      if (s_state != LIGHT_STATE_OFF) {
         set_pwm_duty(0);
         power_pin_set(false);
         s_state = LIGHT_STATE_OFF;
         //LIGHT_LOGI("自动关");
      }
      return;
   }

   uint8_t target = calc_ramp_duty();
   if (target > 0) {
      if (target != s_duty) set_pwm_duty(target);
      if (s_state != LIGHT_STATE_PWM) {
         power_pin_set(true);
         s_state = LIGHT_STATE_PWM;
      }
   } else {
      if (s_state != LIGHT_STATE_OFF) {
         set_pwm_duty(0);
         power_pin_set(false);
         s_state = LIGHT_STATE_OFF;
      }
   }
}

// ---- 时间设置 ----
bool light_control_set_start_time(int h, int m) {
   if (h<0||h>23||m<0||m>59) return false;
   s_sched.start_hour=h; s_sched.start_minute=m;
   int sm=time_to_mins(h,m), em=time_to_mins(s_sched.end_hour,s_sched.end_minute);
   if(em<sm) em+=1440;
   s_sched.duration_hours=(float)(em-sm)/60.0f;
   return true;
}
bool light_control_set_end_time(int h, int m) {
   if (h<0||h>23||m<0||m>59) return false;
   s_sched.end_hour=h; s_sched.end_minute=m;
   int sm=time_to_mins(s_sched.start_hour,s_sched.start_minute), em=time_to_mins(h,m);
   if(em<sm) em+=1440;
   s_sched.duration_hours=(float)(em-sm)/60.0f;
   return true;
}
bool light_control_set_duration(float hours) {
   if (hours<=0||hours>24) return false;
   s_sched.duration_hours=hours;
   int sm=time_to_mins(s_sched.start_hour,s_sched.start_minute);
   int em=sm+(int)(hours*60);
   if(em>=1440) em-=1440;
   s_sched.end_hour=em/60; s_sched.end_minute=em%60;
   return true;
}

// ---- 查询 ----
light_state_t light_control_get_state(void) { return s_state; }
bool light_control_is_on(void) {
   if (s_sched.is_manual_mode) return s_sched.manual_state;
   return s_state != LIGHT_STATE_OFF;
}
bool light_control_is_manual_mode(void) { return s_sched.is_manual_mode; }
int  light_control_get_start_hour(void)   { return s_sched.start_hour; }
int  light_control_get_start_minute(void) { return s_sched.start_minute; }
int  light_control_get_end_hour(void)     { return s_sched.end_hour; }
int  light_control_get_end_minute(void)   { return s_sched.end_minute; }
float light_control_get_duration(void)    { return s_sched.duration_hours; }
uint8_t light_control_get_pwm_duty(void)  { return s_duty; }
int  light_control_get_pwm_pin(int ch)    { return (ch>=0&&ch<LIGHT_CHANNEL_COUNT)?s_sched.pwm_pins[ch]:-1; }