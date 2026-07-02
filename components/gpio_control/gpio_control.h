#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include <stdbool.h>

// 初始化GPIO控制模块
void gpio_control_init(void);

// 反初始化，重置GPIO（可选）
void gpio_test_deinit(void);

// 控制指定GPIO引脚的电平
// 参数: pin - 引脚编号 (必须在初始化列表中)
//        level - 目标电平 (true=高电平/1, false=低电平/0)
// 返回值: true=成功, false=失败 (如引脚无效)
bool gpio_control_set_level(int pin, bool level);

#endif