#ifndef PWM_TEST_H
#define PWM_TEST_H

#include <stdint.h>
#include <stdbool.h>

// 初始化PWM测试模块
void pwm_test_init(int gpio_pin);

// 设置PWM占空比 (0-100%)
bool pwm_test_set_duty(uint8_t duty_percent);

// 停止PWM输出
void pwm_test_stop(void);

// 处理PWM测试命令
void pwm_test_process_command(const char* command);

#endif