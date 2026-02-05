#ifndef LIGHT_CONTROL_H
#define LIGHT_CONTROL_H

#include <stdbool.h>
#include "time_manager.h"  // 依赖时间管理模块

// 补光灯状态枚举
typedef enum {
    LIGHT_STATE_OFF,     // 关闭状态
    LIGHT_STATE_ON,      // 开启状态（无PWM）
    LIGHT_STATE_PWM      // PWM模式（预留）
} light_state_t;

// 初始化补光灯控制模块
void light_control_init(int control_pin);

// 补光灯状态轮询函数（需在主循环中定期调用）
void light_control_poll(void);

// 设置补光灯的每日开启时间（24小时制）
bool light_control_set_start_time(int hour, int minute);

// 设置补光灯的每日关闭时间（24小时制）
bool light_control_set_end_time(int hour, int minute);

// 设置补光灯的总照明时长（小时，支持小数，如3.4）
bool light_control_set_duration(float hours);

// 处理补光灯相关命令（调试用）
void light_control_process_command(const char* command);

// 更新补光灯状态（需在主循环中定期调用）
void light_control_update(void);

// 获取当前补光灯状态
light_state_t light_control_get_state(void);

// 获取补光灯开关状态
bool light_control_is_on(void);

// 直接控制补光灯开关（手动模式）
void light_control_manual_set(bool on);

#endif