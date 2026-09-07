#ifndef GPIO_TEST_H
#define GPIO_TEST_H

#include <stdbool.h>

// 初始化并设置指定GPIO状态
void gpio_test_init(void);

// 反初始化，重置GPIO（可选）
void gpio_test_deinit(void);

// 处理来自input_parser的帧
// 注意：此函数应由主程序在收到新帧后调用
void gpio_control_process_frame(const char* frame);


#endif