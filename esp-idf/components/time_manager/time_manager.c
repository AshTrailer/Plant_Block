#include "time_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "TIME_MGR";

// 内部时间变量
static system_time_t s_current_time = {
    .year = 1970,
    .month = 1,
    .day = 1,
    .hour = 0,
    .minute = 0,
    .second = 0,
    .weekday = 4  // 1970年1月1日是星期四
};

// 互斥锁保护时间数据
static SemaphoreHandle_t s_time_mutex = NULL;

// 时间字符串缓冲区
static char s_time_string[64];

// 时间更新任务句柄
static TaskHandle_t s_time_task_handle = NULL;
static bool s_time_task_running = false;

// 星期字符串数组（中文）
static const char* s_weekday_strings_cn[] = {
    "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
};

// 星期字符串数组（英文缩写）
static const char* s_weekday_strings_en[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

// 判断是否为闰年
static bool is_leap_year(int year) {
    // 闰年规则：能被4整除但不能被100整除，或者能被400整除
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

// 获取指定月份的天数（考虑闰年）
static int get_days_in_month(int year, int month) {
    // 月份天数表（平年）
    static const int days_per_month[] = {
        31, 28, 31, 30, 31, 30, 
        31, 31, 30, 31, 30, 31
    };
    
    if (month < 1 || month > 12) {
        return 0;
    }
    
    // 处理二月（考虑闰年）
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    
    return days_per_month[month - 1];
}

// 边界检查函数
static bool check_time_boundaries(int month, int hour, int minute, int second) {
    if (month < 1 || month > 12) {
        ESP_LOGE(TAG, "Months out of range: %d", month);
        return false;
    }
    if (hour < 0 || hour > 23) {
        ESP_LOGE(TAG, "Hours out of range: %d", hour);
        return false;
    }
    if (minute < 0 || minute > 59) {
        ESP_LOGE(TAG, "Minutes out of range: %d", minute);
        return false;
    }
    if (second < 0 || second > 59) {
        ESP_LOGE(TAG, "Seconds out of range: %d", second);
        return false;
    }
    return true;
}

// 计算星期几的函数（Zeller's congruence算法）
// 返回0=星期日，1=星期一，...，6=星期六
static int calculate_weekday(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year--;
    }
    
    int century = year / 100;
    int year_of_century = year % 100;
    
    // Zeller's congruence公式
    int weekday = (day + (13 * (month + 1)) / 5 + year_of_century + 
                   year_of_century / 4 + century / 4 - 2 * century) % 7;
    
    // 调整结果为0=星期日，1=星期一，...，6=星期六
    return (weekday + 6) % 7;
}

// 更新星期几
static void update_weekday(void) {
    s_current_time.weekday = calculate_weekday(
        s_current_time.year, 
        s_current_time.month, 
        s_current_time.day
    );
}

// 时间递增任务（每秒更新一次）
static void time_update_task(void *arg) {
    ESP_LOGI(TAG, "Time update task started");
    
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 等待1秒
        
        if (xSemaphoreTake(s_time_mutex, portMAX_DELAY) == pdTRUE) {
            // 秒递增
            s_current_time.second++;
            
            // 处理进位：秒→分
            if (s_current_time.second >= 60) {
                s_current_time.second = 0;
                s_current_time.minute++;
                
                // 处理进位：分→时
                if (s_current_time.minute >= 60) {
                    s_current_time.minute = 0;
                    s_current_time.hour++;
                    
                    // 处理进位：时→日
                    if (s_current_time.hour >= 24) {
                        s_current_time.hour = 0;
                        s_current_time.day++;
                        
                        // 日期变化，需要重新计算星期几
                        update_weekday();
                        
                        // 获取当前月份的实际天数
                        int days_in_current_month = get_days_in_month(
                            s_current_time.year, 
                            s_current_time.month
                        );
                        
                        // 处理进位：日→月
                        if (s_current_time.day > days_in_current_month) {
                            s_current_time.day = 1;
                            s_current_time.month++;
                            
                            // 月份变化，需要重新计算星期几
                            update_weekday();
                            
                            // 处理进位：月→年
                            if (s_current_time.month > 12) {
                                s_current_time.month = 1;
                                s_current_time.year++;
                                
                                // 年份变化，需要重新计算星期几
                                update_weekday();
                            }
                        }
                    }
                }
            }
            
            // 更新时间字符串
            snprintf(s_time_string, sizeof(s_time_string),
                    "%04d/%02d/%02d %02d:%02d:%02d %s",
                    s_current_time.year,
                    s_current_time.month,
                    s_current_time.day,
                    s_current_time.hour,
                    s_current_time.minute,
                    s_current_time.second,
                    s_weekday_strings_cn[s_current_time.weekday]);
            
            xSemaphoreGive(s_time_mutex);
        }
    }
}

// 初始化时间管理模块
void time_manager_init(void) {
    // 创建互斥锁
    s_time_mutex = xSemaphoreCreateMutex();
    if (s_time_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create time mutex");
        return;
    }

    // 计算初始时间的星期几
    update_weekday();
    
    // 初始化时间字符串
    snprintf(s_time_string, sizeof(s_time_string),
            "%04d/%02d/%02d %02d:%02d:%02d %s",
            s_current_time.year,
            s_current_time.month,
            s_current_time.day,
            s_current_time.hour,
            s_current_time.minute,
            s_current_time.second,
            s_weekday_strings_cn[s_current_time.weekday]);

    // 创建时间更新任务
    xTaskCreate(time_update_task,
                "time_update_task",
                2048,
                NULL,
                3,  // 中等优先级
                &s_time_task_handle);
    
    if (s_time_task_handle != NULL) {
        s_time_task_running = true;
        ESP_LOGI(TAG, "Time Manager Initialized");
        ESP_LOGI(TAG, "Initial time: %s", s_time_string);
    } else {
        ESP_LOGE(TAG, "Create time update task failed");
    }
}

// 设置系统时间
bool time_manager_set_time(int year, int month, int day, int hour, int minute, int second) {
    // 基础边界检查
    if (!check_time_boundaries(month, hour, minute, second)) {
        return false;
    }
    
    // 年份检查
    if (year < 1970) {
        ESP_LOGE(TAG, "Year %d should not before 1970", year);
        return false;
    }
    
    // 日期有效性检查（基于实际月份天数）
    int max_days = get_days_in_month(year, month);
    if (day < 1 || day > max_days) {
        ESP_LOGE(TAG, "Day %d is out of range for month %d (max: %d)", day, month, max_days);
        return false;
    }
    
    if (xSemaphoreTake(s_time_mutex, portMAX_DELAY) == pdTRUE) {
        s_current_time.year = year;
        s_current_time.month = month;
        s_current_time.day = day;
        s_current_time.hour = hour;
        s_current_time.minute = minute;
        s_current_time.second = second;

        // 更新星期几
        update_weekday();
        
        // 更新时间字符串
        snprintf(s_time_string, sizeof(s_time_string),
                "%04d/%02d/%02d %02d:%02d:%02d %s",
                year, month, day, hour, minute, second,
                s_weekday_strings_cn[s_current_time.weekday]);
        
        xSemaphoreGive(s_time_mutex);
        
        ESP_LOGI(TAG, "Time set to: %s", s_time_string);
        return true;
    }
    
    return false;
}

// 获取当前系统时间
system_time_t time_manager_get_time(void) {
    system_time_t time_copy;
    
    if (xSemaphoreTake(s_time_mutex, portMAX_DELAY) == pdTRUE) {
        time_copy = s_current_time;
        xSemaphoreGive(s_time_mutex);
    }
    
    return time_copy;
}

// 获取时间字符串
const char* time_manager_get_time_string(void) {
    return s_time_string;
}

// 更新时间（供未来的网络同步模块调用）
void time_manager_update_time(int year, int month, int day, int hour, int minute, int second) {
    // 直接调用设置函数，确保边界检查
    time_manager_set_time(year, month, day, hour, minute, second);
}

// 获取当前星期几（0=星期日，1=星期一，...，6=星期六）
int time_manager_get_weekday(void) {
    int weekday;
    
    if (xSemaphoreTake(s_time_mutex, portMAX_DELAY) == pdTRUE) {
        weekday = s_current_time.weekday;
        xSemaphoreGive(s_time_mutex);
    } else {
        weekday = -1; // 错误值
    }
    
    return weekday;
}

// 获取星期几的字符串（中文）
const char* time_manager_get_weekday_string(void) {
    int weekday = time_manager_get_weekday();
    
    if (weekday >= 0 && weekday <= 6) {
        return s_weekday_strings_cn[weekday];
    }
    
    return "未知";
}

// 获取星期几的字符串（英文缩写）
const char* time_manager_get_weekday_string_en(void) {
    int weekday = time_manager_get_weekday();
    
    if (weekday >= 0 && weekday <= 6) {
        return s_weekday_strings_en[weekday];
    }
    
    return "Unknown";
}