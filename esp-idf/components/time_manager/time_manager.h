#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <stdbool.h>

// 时间结构体
typedef struct {
    int year;    // 年份，如 1970
    int month;   // 月份，1-12
    int day;     // 日期，1-31
    int hour;    // 小时，0-23
    int minute;  // 分钟，0-59
    int second;  // 秒，0-59
    int weekday; // 星期几，0=星期日，1=星期一，...，6=星期六
} system_time_t;

// 初始化时间管理模块
void time_manager_init(void);

// 设置系统时间
bool time_manager_set_time(int year, int month, int day, int hour, int minute, int second);

// 获取当前系统时间
system_time_t time_manager_get_time(void);

// 获取时间字符串（格式：YYYY/MM/DD HH:MM:SS）
const char* time_manager_get_time_string(void);

// 更新时间（供未来的网络同步模块调用）
void time_manager_update_time(int year, int month, int day, int hour, int minute, int second);

// 获取当前星期几（0=星期日，1=星期一，...，6=星期六）
int time_manager_get_weekday(void);

// 获取星期几的字符串（中文）
const char* time_manager_get_weekday_string(void);

// 获取星期几的字符串（英文缩写）
const char* time_manager_get_weekday_string_en(void);

#endif