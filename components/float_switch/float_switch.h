#ifndef FLOAT_SWITCH_H
#define FLOAT_SWITCH_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化浮球开关 GPIO
 *
 * 电路：3.3V → 浮球开关 → node1 → GPIO35
 *                      node1 → 10kΩ → GND
 * 闭合 = 高电平，断开 = 低电平（外部下拉）
 *
 * @param gpio_num GPIO 引脚编号（如 35）
 */
void float_switch_init(int gpio_num);

/**
 * @brief 读取浮球开关当前状态（即时读取，无防抖）
 * @return true = 闭合（有水），false = 断开（无水）
 */
bool float_switch_get_state(void);

/**
 * @brief 启动后台监测任务
 *
 * 按 interval_ms 周期轮询 GPIO，仅在状态变化时触发内部回调。
 * 无防抖逻辑，依赖硬件本身无抖动。
 *
 * @param interval_ms 轮询间隔（毫秒），建议 200~1000
 */
void float_switch_start_monitor(uint32_t interval_ms);

/**
 * @brief 停止后台监测任务
 */
void float_switch_stop_monitor(void);

#endif