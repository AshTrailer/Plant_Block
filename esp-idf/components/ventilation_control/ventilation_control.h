#ifndef VENTILATION_CONTROL_H
#define VENTILATION_CONTROL_H

#include <stdbool.h>

// 初始化通风控制模块
// 参数: gpio_pin - 控制通风的GPIO引脚号（默认使用GPIO15）
void ventilation_control_init(int gpio_pin);

// 启动通风控制任务
void ventilation_control_start(void);

// 停止通风控制任务
void ventilation_control_stop(void);

// 设置通风周期参数（用于测试，可以动态调整）
// 参数: vent_on_seconds - 通风开启的秒数
//       vent_off_seconds - 通风关闭的秒数
void ventilation_control_set_timing(int vent_on_seconds, int vent_off_seconds);

// 获取通风控制当前状态
bool ventilation_control_get_state(void);

// 获取通风控制引脚号
int ventilation_control_get_pin(void);

// 获取当前通风开启时长（秒）
int ventilation_control_get_on_seconds(void);

// 获取当前通风关闭时长（秒）
int ventilation_control_get_off_seconds(void);

#endif