#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

// 风扇 ID 枚举
typedef enum {
   FAN_VENTILATION = 0,    // 通风风扇 — 数字开关
   FAN_TEC_COLD,           // TEC 冷端散热风扇 — 数字开关
   FAN_WATER_COOLING,      // 水冷风扇 — 4 线 PWM 调速（0–95%）
   FAN_COUNT               // 风扇总数
} fan_id_t;

// ---------- 初始化 ----------
// 同时配置所有风扇的 GPIO 和 PWM 外设
void fan_control_init(void);

// ---------- 开关控制 ----------
// 对于数字开关风扇（VENTILATION, TEC_COLD）：duty_pct 无意义，传 0 即可
// 对于 PWM 风扇（WATER_COOLING）：duty_pct 范围 0–95（超过 95 则钳位到 95）
void fan_control_set(fan_id_t fan, bool on, uint8_t duty_pct);

// ---------- 状态查询 ----------
bool fan_control_is_on(fan_id_t fan);
uint8_t fan_control_get_duty(fan_id_t fan);

// ---------- 便捷开关（数字风扇）----------
static inline void fan_control_on(fan_id_t fan)  { fan_control_set(fan, true, 95); }
static inline void fan_control_off(fan_id_t fan) { fan_control_set(fan, false, 0); }

#endif // FAN_CONTROL_H