#include "light_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_control.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "LIGHT_CTRL";

// 模块内部状态
typedef struct {
    int start_hour;      // 开启时间-小时 (0-23)
    int start_minute;    // 开启时间-分钟 (0-59)
    int end_hour;        // 关闭时间-小时 (0-23)
    int end_minute;      // 关闭时间-分钟 (0-59)
    float duration_hours; // 总照明时长（小时）
    int control_pin;     // 控制引脚（默认GPIO15）
    bool is_manual_mode; // 是否为手动模式
    bool manual_state;   // 手动模式下的状态
} light_schedule_t;

static light_schedule_t s_schedule = {
    .start_hour = 8,     // 默认早上8点开启
    .start_minute = 0,
    .end_hour = 18,      // 默认晚上6点关闭
    .end_minute = 0,
    .duration_hours = 10.0, // 默认10小时
    .control_pin = 15,
    .is_manual_mode = false,
    .manual_state = false
};

static light_state_t s_current_state = LIGHT_STATE_OFF;
static TickType_t s_last_update_ticks = 0;
static const TickType_t s_update_interval_ticks = 1000 / portTICK_PERIOD_MS; // 1秒

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

// 初始化补光灯控制模块
void light_control_init(int control_pin) {
    s_schedule.control_pin = control_pin;
    
    // 初始化GPIO控制
    // 注意：gpio_control_init() 应该已在主程序中调用
    
    ESP_LOGI(TAG, "补光灯控制模块初始化完成");
    ESP_LOGI(TAG, "控制引脚: GPIO%d", control_pin);
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
        return; // 手动模式下不自动更新
    }
    
    bool should_be_on = should_light_be_on();
    bool is_currently_on = light_control_is_on();
    
    // 状态发生变化时更新
    if (should_be_on != is_currently_on) {
        if (should_be_on) {
            s_current_state = LIGHT_STATE_ON;
            gpio_control_set_level(s_schedule.control_pin, true);
            ESP_LOGI(TAG, "补光灯自动开启");
        } else {
            s_current_state = LIGHT_STATE_OFF;
            gpio_control_set_level(s_schedule.control_pin, false);
            ESP_LOGI(TAG, "补光灯自动关闭");
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
    s_current_state = on ? LIGHT_STATE_ON : LIGHT_STATE_OFF;
    ESP_LOGI(TAG, "补光灯手动%s", on ? "开启" : "关闭");
}

void light_control_poll(void) {
    TickType_t current_ticks = xTaskGetTickCount();
    
    // 检查是否到达更新间隔（1秒）
    if ((current_ticks - s_last_update_ticks) >= s_update_interval_ticks) {
        light_control_update();  // 调用现有的更新函数
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
    ESP_LOGI(TAG, "补光灯切换为自动模式");
}

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