#ifndef FAN_CONTROLLER_H
#define FAN_CONTROLLER_H

#include <stdbool.h>
#include "driver/gpio.h"

// 确保这里的定义是正确的
#define FAN_GPIO_PIN     GPIO_NUM_13

typedef struct {
    bool is_initialized;
    bool current_state;
} fan_controller_t;

/**
 * @brief 初始化风扇控制器
 * @return true: 成功, false: 失败
 */
bool fan_controller_init(void);

/**
 * @brief 设置风扇状态
 * @param state true: 开启, false: 关闭
 * @return true: 成功, false: 失败
 */
bool fan_controller_set_state(bool state);

/**
 * @brief 获取当前风扇状态
 * @return true: 开启, false: 关闭
 */
bool fan_controller_get_state(void);

/**
 * @brief 反转风扇状态
 * @return 新的风扇状态
 */
bool fan_controller_toggle(void);

/**
 * @brief 反初始化风扇控制器
 */
void fan_controller_deinit(void);

#endif // FAN_CONTROLLER_H