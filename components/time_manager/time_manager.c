#include "time_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "cloud_comm.h"

static const char *TAG = "TIME_MGR";

#define TIME_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define TIME_LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define TIME_LOGE(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)


// 内部时间变量
static system_time_t s_current_time = {
    .year = 1970,
    .month = 1,
    .day = 1,
    .hour = 0,
    .minute = 0,
    .second = 0,
    .weekday = 4,  // 1970年1月1日是星期四
    .unix_time = 0
};

// 互斥锁保护时间数据
static SemaphoreHandle_t s_time_mutex = NULL;

// 时间字符串缓冲区
static char s_time_string[64];

// 时间更新任务句柄
static TaskHandle_t s_time_task_handle = NULL;
static bool s_time_task_running = false;

// 计算指定日期的ISO周数
int time_manager_get_iso_week_number(int year, int month, int day) {
    // 计算该日期是一年中的第几天
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    // 闰年检查
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[1] = 29;
    }
    
    int day_of_year = day;
    for (int i = 0; i < month - 1; i++) {
        day_of_year += days_in_month[i];
    }
    
    // 计算该日期的星期几（使用Zeller's congruence）
    int y = year;
    int m = month;
    int d = day;
    
    if (m < 3) {
        m += 12;
        y--;
    }
    
    int century = y / 100;
    int year_of_century = y % 100;
    
    int weekday = (d + (13 * (m + 1)) / 5 + year_of_century + 
                   year_of_century / 4 + century / 4 - 2 * century) % 7;
    
    // 调整结果为0=星期六，1=星期日，...，6=星期五
    weekday = (weekday + 6) % 7;
    
    // ISO周数计算公式
    int iso_week = (day_of_year - weekday + 10) / 7;
    
    // 边界处理
    if (iso_week < 1) {
        // 上一年的最后一周
        // 简单处理，实际需要更精确的计算
        iso_week = 52;
    } else if (iso_week > 52) {
        // 下一年的第一周
        iso_week = 1;
    }
    
    return iso_week;
}

// 获取当前日期的ISO周数
int time_manager_get_current_iso_week(void) {
    system_time_t now = time_manager_get_time();
    return time_manager_get_iso_week_number(now.year, now.month, now.day);
}

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

// 计算指定日期到1970年1月1日的天数
static int days_since_1970(int year, int month, int day) {
    int days = 0;
    
    // 计算从1970年到year-1年的天数
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    
    // 计算当年1月1日到当前月份的天数
    for (int m = 1; m < month; m++) {
        days += get_days_in_month(year, m);
    }
    
    // 加上当前月的天数（day从1开始，所以需要减1）
    days += (day - 1);
    
    return days;
}

// 计算Unix时间戳（从1970年1月1日00:00:00开始的秒数）
static time_t calculate_unix_time(int year, int month, int day, int hour, int minute, int second) {
    // 计算天数
    int days = days_since_1970(year, month, day);
    
    // 转换为秒数
    time_t unix_time = (time_t)days * 24 * 3600;
    unix_time += hour * 3600;
    unix_time += minute * 60;
    unix_time += second;
    
    return unix_time;
}

// 边界检查函数
static bool check_time_boundaries(int month, int hour, int minute, int second) {
    if (month < 1 || month > 12) {
        TIME_LOGE("Months out of range: %d", month);
        return false;
    }
    if (hour < 0 || hour > 23) {
        TIME_LOGE("Hours out of range: %d", hour);
        return false;
    }
    if (minute < 0 || minute > 59) {
        TIME_LOGE("Minutes out of range: %d", minute);
        return false;
    }
    if (second < 0 || second > 59) {
        TIME_LOGE("Seconds out of range: %d", second);
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

// 从Unix时间戳解析为日期时间
static void parse_unix_time(time_t unix_time, system_time_t* time_struct) {
    time_t remaining_seconds = unix_time;
    
    // 计算年份
    int year = 1970;
    while (1) {
        int days_in_year = is_leap_year(year) ? 366 : 365;
        time_t seconds_in_year = days_in_year * 24 * 3600;
        
        if (remaining_seconds < seconds_in_year) {
            break;
        }
        remaining_seconds -= seconds_in_year;
        year++;
    }
    
    // 计算月份
    int month = 1;
    while (1) {
        int days_in_month = get_days_in_month(year, month);
        time_t seconds_in_month = days_in_month * 24 * 3600;
        
        if (remaining_seconds < seconds_in_month) {
            break;
        }
        remaining_seconds -= seconds_in_month;
        month++;
    }
    
    // 计算天数（注意：剩余秒数现在是该月第0天的秒数）
    int day = (int)(remaining_seconds / (24 * 3600)) + 1;
    remaining_seconds %= (24 * 3600);
    
    // 计算小时、分钟、秒
    int hour = (int)(remaining_seconds / 3600);
    remaining_seconds %= 3600;
    
    int minute = (int)(remaining_seconds / 60);
    int second = (int)(remaining_seconds % 60);
    
    // 计算星期几
    int weekday = calculate_weekday(year, month, day);
    
    // 填充结构体
    time_struct->year = year;
    time_struct->month = month;
    time_struct->day = day;
    time_struct->hour = hour;
    time_struct->minute = minute;
    time_struct->second = second;
    time_struct->weekday = weekday;
    time_struct->unix_time = unix_time;
}

// 更新星期几和Unix时间戳
static void update_time_metadata(void) {
    // 更新星期几
    s_current_time.weekday = calculate_weekday(
        s_current_time.year, 
        s_current_time.month, 
        s_current_time.day
    );
    
    // 更新Unix时间戳
    s_current_time.unix_time = calculate_unix_time(
        s_current_time.year,
        s_current_time.month,
        s_current_time.day,
        s_current_time.hour,
        s_current_time.minute,
        s_current_time.second
    );
}

// 更新时间字符串
static void update_time_string(void) {
    snprintf(s_time_string, sizeof(s_time_string),
            "%04d/%02d/%02d %02d:%02d:%02d %s",
            s_current_time.year,
            s_current_time.month,
            s_current_time.day,
            s_current_time.hour,
            s_current_time.minute,
            s_current_time.second,
            s_weekday_strings_cn[s_current_time.weekday]);
}

// 时间递增任务（每秒更新一次）
static void time_update_task(void *arg) {
    TIME_LOGI("Time update task started");
    
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
                        
                        // 日期变化，需要重新计算星期几和Unix时间戳
                        update_time_metadata();
                        
                        // 获取当前月份的实际天数
                        int days_in_current_month = get_days_in_month(
                            s_current_time.year, 
                            s_current_time.month
                        );
                        
                        // 处理进位：日→月
                        if (s_current_time.day > days_in_current_month) {
                            s_current_time.day = 1;
                            s_current_time.month++;
                            
                            // 月份变化，需要重新计算星期几和Unix时间戳
                            update_time_metadata();
                            
                            // 处理进位：月→年
                            if (s_current_time.month > 12) {
                                s_current_time.month = 1;
                                s_current_time.year++;
                                
                                // 年份变化，需要重新计算星期几和Unix时间戳
                                update_time_metadata();
                            }
                        }
                    }
                }
            }
            
            // 更新Unix时间戳（简单递增）
            s_current_time.unix_time++;
            
            // 更新时间字符串
            update_time_string();
            
            xSemaphoreGive(s_time_mutex);
        }
    }
}

// 初始化时间管理模块
void time_manager_init(void) {
    // 创建互斥锁
    s_time_mutex = xSemaphoreCreateMutex();
    if (s_time_mutex == NULL) {
        TIME_LOGE("Failed to create time mutex");
        return;
    }

    // 计算初始时间的星期几和Unix时间戳
    update_time_metadata();
    
    // 初始化时间字符串
    update_time_string();

    // 创建时间更新任务
    xTaskCreate(time_update_task,
                "time_update_task",
                2048,
                NULL,
                3,  // 中等优先级
                &s_time_task_handle);
    
    if (s_time_task_handle != NULL) {
        s_time_task_running = true;
        TIME_LOGI("Time Manager Initialized");
        TIME_LOGI("Initial time: %s", s_time_string);
        TIME_LOGI("Unix timestamp: %ld", s_current_time.unix_time);
    } else {
        TIME_LOGE("Create time update task failed");
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
        TIME_LOGE("Year %d should not before 1970", year);
        return false;
    }
    
    // 日期有效性检查（基于实际月份天数）
    int max_days = get_days_in_month(year, month);
    if (day < 1 || day > max_days) {
        TIME_LOGE("Day %d is out of range for month %d (max: %d)", day, month, max_days);
        return false;
    }
    
    if (xSemaphoreTake(s_time_mutex, portMAX_DELAY) == pdTRUE) {
        s_current_time.year = year;
        s_current_time.month = month;
        s_current_time.day = day;
        s_current_time.hour = hour;
        s_current_time.minute = minute;
        s_current_time.second = second;

        // 更新星期几和Unix时间戳
        update_time_metadata();
        
        // 更新时间字符串
        update_time_string();
        
        xSemaphoreGive(s_time_mutex);
        
        ESP_LOGI(TAG, "Time set to: %s", s_time_string);
        ESP_LOGI(TAG, "Unix timestamp: %ld", s_current_time.unix_time);
        return true;
    }
    
    return false;
}

// 从Unix时间戳设置系统时间
bool time_manager_set_time_from_unix(time_t unix_time) {
    if (unix_time < 0) {
        TIME_LOGE("Unix time should be positive");
        return false;
    }
    
    if (xSemaphoreTake(s_time_mutex, portMAX_DELAY) == pdTRUE) {
        // 解析Unix时间戳
        parse_unix_time(unix_time, &s_current_time);
        
        // 更新时间字符串
        update_time_string();
        
        xSemaphoreGive(s_time_mutex);
        
        ESP_LOGI(TAG, "Time set from Unix timestamp: %s", s_time_string);
        ESP_LOGI(TAG, "Unix timestamp: %ld", s_current_time.unix_time);
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

// 获取当前Unix时间戳
time_t time_manager_get_unix_time(void) {
    time_t unix_time = 0;
    
    if (xSemaphoreTake(s_time_mutex, portMAX_DELAY) == pdTRUE) {
        unix_time = s_current_time.unix_time;
        xSemaphoreGive(s_time_mutex);
    }
    
    return unix_time;
}

// 获取时间字符串
const char* time_manager_get_time_string(void) {
    return s_time_string;
}

// 线程安全地获取时间字符串
void time_manager_get_time_string_safe(char* buffer, size_t max_len) {
    if (buffer == NULL || max_len == 0) return;
    
    if (xSemaphoreTake(s_time_mutex, portMAX_DELAY) == pdTRUE) {
        strncpy(buffer, s_time_string, max_len - 1);
        buffer[max_len - 1] = '\0';
        xSemaphoreGive(s_time_mutex);
    } else {
        buffer[0] = '\0';
    }
}

// 更新时间
void time_manager_update_time(int year, int month, int day, int hour, int minute, int second) {
    // 直接调用设置函数，确保边界检查
    time_manager_set_time(year, month, day, hour, minute, second);
    ESP_LOGI(TAG, "当前时间: %s", time_manager_get_time_string());
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