#include "fan_control.h"
#include "pin_definitions.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "FAN_CTRL";

// ---------- 风扇配置表 ----------
typedef struct {
   const char *name;
   int gpio;            // 控制引脚（-1 表示未分配）
   bool is_pwm;         // true=LEDC PWM, false=GPIO 数字开关
   ledc_channel_t ledc_channel;
   uint8_t max_duty;    // 最大占空比百分比
} fan_config_t;

static fan_config_t s_fans[FAN_COUNT] = {
   [FAN_VENTILATION]   = {"ventilation", PIN_VENTILATION_FAN,  false, LEDC_CHANNEL_0, 100},
   [FAN_TEC_COLD]      = {"tec_cold",    PIN_TEC_COLD_FAN,     false, LEDC_CHANNEL_1, 100},
   [FAN_WATER_COOLING] = {"water_cool",  PIN_WATER_FAN_PWM,    true,  LEDC_CHANNEL_2, 95},
};

// ---------- 运行状态 ----------
typedef struct {
   bool on;
   uint8_t duty_pct;
} fan_state_t;

static fan_state_t s_states[FAN_COUNT];
static bool s_initialized = false;

// ---------- LEDC 定时器配置 ----------
#define FAN_LEDC_TIMER       LEDC_TIMER_1
#define FAN_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define FAN_LEDC_FREQ_HZ     25000        // 25kHz，超出人耳可听范围
#define FAN_LEDC_RESOLUTION  LEDC_TIMER_10_BIT
#define FAN_LEDC_MAX_DUTY    1023         // 2^10 - 1

// ---------- 初始化 ----------
void fan_control_init(void)
{
   // 1. 配置 LEDC 定时器（PWM 风扇共用）
   ledc_timer_config_t timer_cfg = {
      .speed_mode      = FAN_LEDC_MODE,
      .timer_num       = FAN_LEDC_TIMER,
      .duty_resolution = FAN_LEDC_RESOLUTION,
      .freq_hz         = FAN_LEDC_FREQ_HZ,
      .clk_cfg         = LEDC_AUTO_CLK,
   };
   ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

   // 2. 初始化各风扇
   for (int i = 0; i < FAN_COUNT; i++) {
      fan_config_t *cfg = &s_fans[i];
      s_states[i].on = false;
      s_states[i].duty_pct = 0;

      if (cfg->gpio < 0) {
         ESP_LOGW(TAG, "Fan '%s' GPIO not assigned, skipping", cfg->name);
         continue;
      }

      if (cfg->is_pwm) {
         // PWM 通道配置
         ledc_channel_config_t ch_cfg = {
            .gpio_num   = cfg->gpio,
            .speed_mode = FAN_LEDC_MODE,
            .channel    = cfg->ledc_channel,
            .intr_type  = LEDC_INTR_DISABLE,
            .timer_sel  = FAN_LEDC_TIMER,
            .duty       = 0,
            .hpoint     = 0,
         };
         ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
         ESP_LOGI(TAG, "Fan '%s' initialized as PWM on GPIO%d (max %d%%)",
                  cfg->name, cfg->gpio, cfg->max_duty);
      } else {
         // 数字 GPIO 配置
         gpio_config_t io_cfg = {
            .pin_bit_mask = (1ULL << cfg->gpio),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,  // 默认下拉确保关断
            .intr_type    = GPIO_INTR_DISABLE,
         };
         ESP_ERROR_CHECK(gpio_config(&io_cfg));
         gpio_set_level(cfg->gpio, 0);
         ESP_LOGI(TAG, "Fan '%s' initialized as digital on GPIO%d",
                  cfg->name, cfg->gpio);
      }
   }

   // 3. 配置 TACH 引脚（仅输入，可选）
   gpio_config_t tach_cfg = {
      .pin_bit_mask = (1ULL << PIN_WATER_FAN_TACH),
      .mode         = GPIO_MODE_INPUT,
      .pull_up_en   = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type    = GPIO_INTR_DISABLE,
   };
   gpio_config(&tach_cfg);
   ESP_LOGI(TAG, "Water cooling fan TACH input on GPIO%d", PIN_WATER_FAN_TACH);

   s_initialized = true;
   ESP_LOGI(TAG, "Fan control module initialized (%d fans)", FAN_COUNT);
}

// ---------- 设置风扇 ----------
void fan_control_set(fan_id_t fan, bool on, uint8_t duty_pct)
{
   if (!s_initialized || fan >= FAN_COUNT) return;

   fan_config_t *cfg = &s_fans[fan];

   // 钳位占空比
   if (duty_pct > cfg->max_duty) duty_pct = cfg->max_duty;
   if (!on) duty_pct = 0;

   // 状态去抖（避免重复设置）
   if (s_states[fan].on == on && s_states[fan].duty_pct == duty_pct) return;

   s_states[fan].on = on;
   s_states[fan].duty_pct = duty_pct;

   if (cfg->is_pwm) {
      uint32_t duty_val = (uint32_t)((float)duty_pct / 100.0f * FAN_LEDC_MAX_DUTY);
      ledc_set_duty(FAN_LEDC_MODE, cfg->ledc_channel, duty_val);
      ledc_update_duty(FAN_LEDC_MODE, cfg->ledc_channel);
   } else {
      gpio_set_level(cfg->gpio, on ? 1 : 0);
   }

   //ESP_LOGI(TAG, "Fan '%s': %s, duty=%d%%", cfg->name, on ? "ON" : "OFF", duty_pct);
}

// ---------- 状态查询 ----------
bool fan_control_is_on(fan_id_t fan)
{
   if (fan >= FAN_COUNT) return false;
   return s_states[fan].on;
}

uint8_t fan_control_get_duty(fan_id_t fan)
{
   if (fan >= FAN_COUNT) return 0;
   return s_states[fan].duty_pct;
}