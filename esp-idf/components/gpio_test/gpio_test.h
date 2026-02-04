#ifndef GPIO_TEST_H
#define GPIO_TEST_H

#include <stdbool.h>

// 初始化并设置指定GPIO状态
void gpio_test_init(void);

// 反初始化，重置GPIO（可选）
void gpio_test_deinit(void);

#endif