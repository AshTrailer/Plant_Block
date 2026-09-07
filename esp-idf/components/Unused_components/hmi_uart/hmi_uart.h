#ifndef HMI_UART_H
#define HMI_UART_H

#include <stdbool.h>

/**
 * @brief 初始化 HMI 串口模块
 * @param tx_pin   TX 引脚 (默认 GPIO16)
 * @param rx_pin   RX 引脚 (默认 GPIO17)
 */
void hmi_uart_init(int tx_pin, int rx_pin);

/**
 * @brief 轮询函数，需在主循环中定期调用
 * 负责接收数据、组帧并处理
 */
void hmi_uart_poll(void);

#endif // HMI_UART_H