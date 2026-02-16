#include "light_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_control.h"
#include "driver/ledc.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "LIGHT_CTRL";

// PWM配置
#define PWM_TIMER          LEDC_TIMER_0
#define PWM_MODE           LEDC_LOW_SPEED_MODE
#define PWM_CHANNEL        LEDC_CHANNEL_0
#define PWM_FREQUENCY_HZ   19000     // 19kHz
#define PWM_RESOLUTION     LEDC_TIMER_12_BIT//LEDC_TIMER_10_BIT
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
    int control_pin;     // 开关控制引脚（GPIO15）
    int pwm_pin;         // PWM控制引脚（GPIO14）
    bool is_manual_mode; // 是否为手动模式
    bool manual_state;   // 手动模式下的状态
} light_schedule_t;

static light_schedule_t s_schedule = {
    .start_hour = 8,     // 默认早上8点开启
    .start_minute = 0,
    .end_hour = 18,      // 默认晚上6点关闭
    .end_minute = 0,
    .duration_hours = 10.0, // 默认10小时
    .control_pin = 9,
    .pwm_pin = 14,
    .is_manual_mode = false,
    .manual_state = false
};

static light_state_t s_current_state = LIGHT_STATE_OFF;
static uint8_t s_current_pwm_duty = 0;  // 当前PWM占空比（0-100%）
static TickType_t s_last_update_ticks = 0;
static TickType_t s_last_pwm_update_ticks = 0;
static const TickType_t s_update_interval_ticks = 1000 / portTICK_PERIOD_MS; // 1秒
static const TickType_t s_pwm_update_interval_ticks = 30000 / portTICK_PERIOD_MS; // 30秒（PWM更新）
static bool s_pwm_initialized = false;

// 检查时间参数有效性
static bool check_time_params(int hour, int minute) {
    if (hour < 0 || hour > 23) {
        ESP_LOGE(TAG, "小时超出范围: %d (应为0-23)", hour);
        return false;
    }
    if (minute < 0 || minute > 59) {
        ESP_LOGE(TAG, "分钟超出范围: %d (应为0-59)", minute);
        return false;
    }
    return true;
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

// 设置PWM占空比（0-100%）
static bool set_pwm_duty(uint8_t duty_percent) {
    if (!s_pwm_initialized) {
        ESP_LOGE(TAG, "PWM未初始化");
        return false;
    }
    
    if (duty_percent > PWM_MAX_DUTY_PERCENT) {
        ESP_LOGE(TAG, "占空比超出范围: %d (0-100)", duty_percent);
        return false;
    }
    
    // 计算实际占空比值
    uint32_t duty_value = 0;
    if (duty_percent > 0) {
        duty_value = (uint32_t)((float)duty_percent / 100.0f * (float)PWM_MAX_DUTY);
    }
    
    // 更新占空比
    esp_err_t ret = ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置PWM占空比失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 更新输出
    ret = ledc_update_duty(PWM_MODE, PWM_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "更新PWM输出失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    s_current_pwm_duty = duty_percent;
    
    // ESP_LOGI(TAG, "PWM占空比设置为: %d%% (值: %d)", duty_percent, duty_value);
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

// 初始化PWM
static bool pwm_init(void) {
    if (s_pwm_initialized) {
        ESP_LOGW(TAG, "PWM已经初始化");
        return true;
    }
    
    // 配置定时器
    ledc_timer_config_t timer_config = {
        .speed_mode       = PWM_MODE,
        .timer_num        = PWM_TIMER,
        .duty_resolution  = PWM_RESOLUTION,
        .freq_hz          = PWM_FREQUENCY_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PWM定时器配置失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 配置通道
    ledc_channel_config_t channel_config = {
        .gpio_num       = s_schedule.pwm_pin,
        .speed_mode     = PWM_MODE,
        .channel        = PWM_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = PWM_TIMER,
        .duty           = 0,  // 初始占空比为0
        .hpoint         = 0
    };
    
    ret = ledc_channel_config(&channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PWM通道配置失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    s_pwm_initialized = true;
    s_current_pwm_duty = 0;
    
    ESP_LOGI(TAG, "PWM初始化完成");
    ESP_LOGI(TAG, "PWM引脚: GPIO%d", s_schedule.pwm_pin);
    ESP_LOGI(TAG, "频率: %d Hz", PWM_FREQUENCY_HZ);
    ESP_LOGI(TAG, "分辨率: 12位 (0-%d)", PWM_MAX_DUTY);
    ESP_LOGI(TAG, "最小占空比: %d%%", PWM_MIN_DUTY_PERCENT);
    
    return true;
}

// 初始化补光灯控制模块
void light_control_init(int control_pin, int pwm_pin) {
    s_schedule.control_pin = control_pin;
    s_schedule.pwm_pin = pwm_pin;
    
    // 初始化GPIO控制
    // 注意：gpio_control_init() 应该已在主程序中调用
    
    // 初始化PWM
    pwm_init();
    
    ESP_LOGI(TAG, "补光灯控制模块初始化完成");
    ESP_LOGI(TAG, "开关控制引脚: GPIO%d", control_pin);
    ESP_LOGI(TAG, "PWM调光引脚: GPIO%d", pwm_pin);
    ESP_LOGI(TAG, "默认计划: %02d:%02d - %02d:%02d (%.1f小时)", 
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
    
    ESP_LOGI(TAG, "开启时间已设置为: %02d:%02d", hour, minute);
    ESP_LOGI(TAG, "自动计算照明时长: %.1f小时", s_schedule.duration_hours);
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
    
    ESP_LOGI(TAG, "关闭时间已设置为: %02d:%02d", hour, minute);
    ESP_LOGI(TAG, "自动计算照明时长: %.1f小时", s_schedule.duration_hours);
    return true;
}

// 设置补光灯的总照明时长
bool light_control_set_duration(float hours) {
    if (hours <= 0 || hours > 24) {
        ESP_LOGE(TAG, "照明时长超出范围: %.1f (应为0-24小时)", hours);
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
    
    ESP_LOGI(TAG, "照明时长已设置为: %.1f小时", hours);
    ESP_LOGI(TAG, "自动调整关闭时间: %02d:%02d", 
             s_schedule.end_hour, s_schedule.end_minute);
    return true;
}

// 更新补光灯状态
void light_control_update(void) {
    if (s_schedule.is_manual_mode) {
        // 手动模式下，PWM保持100%（或最后设置的值），不进行自动调光
        return;
    }
    
    bool should_be_on = should_light_be_on();
    bool is_currently_on = light_control_is_on();
    
    // 状态发生变化时更新（每秒检查）
    if (should_be_on != is_currently_on) {
        if (should_be_on) {
            s_current_state = LIGHT_STATE_PWM; // 自动模式下使用PWM
            gpio_control_set_level(s_schedule.control_pin, true); // 打开主开关
            ESP_LOGI(TAG, "补光灯自动开启");
            
            // 开启时立即更新一次PWM
            uint8_t target_duty = calculate_auto_pwm_duty();
            set_pwm_duty(target_duty);
        } else {
            s_current_state = LIGHT_STATE_OFF;
            gpio_control_set_level(s_schedule.control_pin, false); // 关闭主开关
            set_pwm_duty(0); // 关闭PWM输出
            ESP_LOGI(TAG, "补光灯自动关闭");
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
            set_pwm_duty(target_duty);
            ESP_LOGI(TAG, "更新光照PWM: %d%%", target_duty);
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
    gpio_control_set_level(s_schedule.control_pin, on);
    
    if (on) {
        s_current_state = LIGHT_STATE_ON;
        // 手动模式下，PWM设置为100%
        set_pwm_duty(100);
        ESP_LOGI(TAG, "补光灯手动开启，PWM: 100%%");
    } else {
        s_current_state = LIGHT_STATE_OFF;
        // 关闭时，PWM设置为0
        set_pwm_duty(0);
        ESP_LOGI(TAG, "补光灯手动关闭");
    }
}

// 轮询函数
void light_control_poll(void) {
    TickType_t current_ticks = xTaskGetTickCount();
    
    // 检查是否到达开关状态更新间隔（1秒）
    if ((current_ticks - s_last_update_ticks) >= s_update_interval_ticks) {
        light_control_update();  // 调用更新函数
        s_last_update_ticks = current_ticks;
    }
    
    // 检查是否到达PWM更新间隔（30秒）
    if ((current_ticks - s_last_pwm_update_ticks) >= s_pwm_update_interval_ticks) {
        update_pwm_duty();  // 调用PWM更新函数
        s_last_pwm_update_ticks = current_ticks;
    }
}

// 获取当前是否为手动模式
bool light_control_is_manual_mode(void) {
    return s_schedule.is_manual_mode;
}

// 设置自动模式
void light_control_set_auto_mode(void) {
    s_schedule.is_manual_mode = false;
    ESP_LOGI(TAG, "补光灯切换为自动模式");
    
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

// 获取补光灯控制引脚号（开关引脚）
int light_control_get_pin(void) {
    return s_schedule.control_pin;
}

// 获取当前PWM占空比
uint8_t light_control_get_pwm_duty(void) {
    return s_current_pwm_duty;
}

// 获取PWM控制引脚号
int light_control_get_pwm_pin(void) {
    return s_schedule.pwm_pin;
}