#include "hmi_uart.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "HMI_UART";

// 缓冲区大小
#define HMI_BUF_SIZE 128

// UART 配置
#define HMI_UART_NUM      UART_NUM_1
#define HMI_UART_BAUD     115200
#define HMI_TXD_PIN       GPIO_NUM_16
#define HMI_RXD_PIN       GPIO_NUM_17
#define HMI_UART_BUF_SIZE 256

// 解析状态机
typedef enum {
    WAIT_START,    // 等待 'S'
    WAIT_PREFIX,   // 接收 "SET_N0:" 剩余部分
    WAIT_DATA      // 接收4字节数据
} parser_state_t;

static parser_state_t s_state = WAIT_START;
static char s_prefix_buf[7];   // 存储 "SET_N0:"
static int s_prefix_idx = 0;
static uint8_t s_data_buf[4];  // 存储4字节数值（小端）
static int s_data_idx = 0;

// 初始化 UART
void hmi_uart_init(int tx_pin, int rx_pin)
{
    uart_config_t uart_config = {
        .baud_rate = HMI_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(HMI_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(HMI_UART_NUM, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(HMI_UART_NUM, HMI_UART_BUF_SIZE, 0, 0, NULL, 0));

    ESP_LOGI(TAG, "HMI UART initialized on TX:%d, RX:%d", tx_pin, rx_pin);
}

// 轮询函数
void hmi_uart_poll(void)
{
    uint8_t data;
    int len = uart_read_bytes(HMI_UART_NUM, &data, 1, 0); // 非阻塞读取一个字节

    while (len > 0) {
        char c = (char)data;

        switch (s_state) {
            case WAIT_START:
                if (c == 'S') {
                    s_prefix_idx = 1;
                    s_prefix_buf[0] = c;
                    s_state = WAIT_PREFIX;
                }
                break;

            case WAIT_PREFIX:
                if (s_prefix_idx < 7) {
                    s_prefix_buf[s_prefix_idx++] = c;
                }
                if (s_prefix_idx == 7) {
                    if (memcmp(s_prefix_buf, "SET_N0:", 7) == 0) {
                        // 前缀正确，准备接收数据
                        s_state = WAIT_DATA;
                        s_data_idx = 0;
                    } else {
                        // 前缀错误，重新开始
                        s_state = WAIT_START;
                    }
                }
                break;

            case WAIT_DATA:
                s_data_buf[s_data_idx++] = c;
                if (s_data_idx == 4) {
                    // 4字节数据接收完毕，解析小端整数
                    int value = s_data_buf[0] 
                              | (s_data_buf[1] << 8) 
                              | (s_data_buf[2] << 16) 
                              | (s_data_buf[3] << 24);
                    ESP_LOGI(TAG, "Received value: %d (0x%02X %02X %02X %02X)",
                             value, s_data_buf[0], s_data_buf[1], s_data_buf[2], s_data_buf[3]);
                    // 重置状态，等待下一帧
                    s_state = WAIT_START;
                }
                break;
        }

        len = uart_read_bytes(HMI_UART_NUM, &data, 1, 0);
    }
}