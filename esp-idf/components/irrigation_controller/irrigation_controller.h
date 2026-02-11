#ifndef IRRIGATION_CONTROLLER_H
#define IRRIGATION_CONTROLLER_H

#include <stdbool.h>

// 初始化浇水控制模块
void irrigation_controller_init(int pump_pin);

// 浇水控制轮询函数（需在主循环中调用）
void irrigation_controller_poll(void);

// 模拟土壤湿度输入（用于测试）
void irrigation_controller_set_moisture_sim(int moisture);

// 设置触发阈值
bool irrigation_controller_set_threshold(int threshold);

// 设置单次浇水时长
bool irrigation_controller_set_duration(float seconds);

// 设置周最小浇水次数
bool irrigation_controller_set_week_min(int min_times);

// 设置周最大浇水次数
bool irrigation_controller_set_week_max(int max_times);

// 获取当前设置
int irrigation_controller_get_threshold(void);
float irrigation_controller_get_duration(void);
int irrigation_controller_get_week_min(void);
int irrigation_controller_get_week_max(void);
int irrigation_controller_get_week_count(void);

// 手动触发浇水（测试用）
void irrigation_controller_manual_trigger(void);

// 获取传感器启动信号状态
bool irrigation_controller_get_sensor_power_status(void);

// 重置本周浇水次数
bool irrigation_controller_reset_week(void);

#endif