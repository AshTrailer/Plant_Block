#ifndef VOFA_OUTPUT_H
#define VOFA_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "event_bus.h"

// ---------- 初始化 ----------
// uart_num: UART 端口号（通常为 UART_NUM_0）
// baud_rate: 波特率（建议 115200 或更高）
void vofa_output_init(int uart_num, int baud_rate);

// ---------- 发送 CSV 格式数据帧 ----------
// 格式: "prefix:val1,val2,...,valN\n"
// prefix 可为 NULL，此时格式为 "val1,val2,...,valN\n"
void vofa_output_send(const char *prefix, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// ---------- 便捷函数：发送传感器数据帧 ----------
// 内部按固定顺序拼装所有传感器数值，Vofa+ 中可拖拽曲线
void vofa_output_send_sensor_frame(float sht30_temp, float sht30_hum,
                                   float ds18_cold, float ds18_hot,
                                   float moisture_pct, uint32_t moisture_mv,
                                   bool float_has_water, bool ntc_overtemp,
                                   uint8_t light_duty, bool light_on,
                                   uint8_t pump_speed, bool pump_on);

// ---------- 事件总线订阅者（可选）----------
// 注册后自动监听事件并输出到 Vofa+
void vofa_output_subscribe_all(void);

#endif // VOFA_OUTPUT_H