#ifndef LIGHT_CONTROL_H
#define LIGHT_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "time_manager.h"  // 依赖时间管理模块

// 补光灯状态枚举
typedef enum {
   LIGHT_STATE_OFF,     // 关闭状态
   LIGHT_STATE_ON,      // 开启状态（无PWM）
   LIGHT_STATE_PWM      // PWM模式
} light_state_t;

// 定义通道数量
#define LIGHT_CHANNEL_COUNT 1

// 初始化补光灯控制模块（四通道）
void light_control_init(const int pwm_pins[LIGHT_CHANNEL_COUNT], int fan_pin);

// 补光灯状态轮询函数（需在主循环中定期调用）
void light_control_poll(void);

// 设置补光灯的每日开启时间（24小时制）
bool light_control_set_start_time(int hour, int minute);

// 设置补光灯的每日关闭时间（24小时制）
bool light_control_set_end_time(int hour, int minute);

// 设置补光灯的总照明时长（小时，支持小数，如3.4）
bool light_control_set_duration(float hours);

// 更新补光灯状态（需在主循环中定期调用）
void light_control_update(void);

// 获取当前补光灯状态
light_state_t light_control_get_state(void);

// 获取补光灯开关状态
bool light_control_is_on(void);

// 直接控制补光灯开关（手动模式）
void light_control_manual_set(bool on);

// 获取当前是否为手动模式
bool light_control_is_manual_mode(void);

// 设置自动模式
void light_control_set_auto_mode(void);

// 获取开启时间 - 小时
int light_control_get_start_hour(void);

// 获取开启时间 - 分钟
int light_control_get_start_minute(void);

// 获取关闭时间 - 小时
int light_control_get_end_hour(void);

// 获取关闭时间 - 分钟
int light_control_get_end_minute(void);

// 获取照明时长
float light_control_get_duration(void);

// 获取当前PWM占空比
uint8_t light_control_get_pwm_duty(void);

// 获取指定通道的PWM控制引脚号
int light_control_get_pwm_pin(int channel);

// 获取散热风扇引脚的函数
int light_control_get_fan_pin(void);

#endif