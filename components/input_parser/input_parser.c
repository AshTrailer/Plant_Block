#include "input_parser.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "cloud_comm.h"

static const char *TAG = "INPUT";

#define INPUT_LOGI(fmt, ...) do { \
    ESP_LOGI(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[I] " fmt, ##__VA_ARGS__); \
} while(0)

#define INPUT_LOGE(fmt, ...) do { \
    ESP_LOGE(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[E] " fmt, ##__VA_ARGS__); \
} while(0)

#define INPUT_LOGW(fmt, ...) do { \
    ESP_LOGW(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[W] " fmt, ##__VA_ARGS__); \
} while(0)

#define INPUT_BUFFER_SIZE 128
static char s_input_buffer[INPUT_BUFFER_SIZE];
static bool s_timeout_enabled = false;
static input_parser_mode_t s_current_mode = INPUT_MODE_BLOCKING;

// --- 非阻塞模式状态机变量 ---
static int s_buffer_index = 0;          // 当前缓冲区写入位置
static bool s_frame_ready = false;      // 帧就绪标志
static bool s_in_parsing = false;       // 是否正在接收一帧

// 非阻塞模式轮询函数
void input_parser_poll(void) {
    if (s_current_mode != INPUT_MODE_NON_BLOCKING) return;

    int c;
    // 只尝试读取一个字符，避免阻塞
    c = getchar();

    if (c == EOF) {
        // 没有字符，立即返回，不阻塞
        return;
    }

    // 字符处理逻辑
    // 回车键处理：结束组帧
    if (c == '\n' || c == '\r') {
        if (s_buffer_index == 0) {
            INPUT_LOGI("Received empty frame, ignoring.");
        } else {
            s_input_buffer[s_buffer_index] = '\0';
            INPUT_LOGI("Frame ready: %s", s_input_buffer);
            s_frame_ready = true; // 关键：设置标志位
        }
        s_in_parsing = false;
        s_buffer_index = 0;
        return;
    }

    // 如果是新帧的开始
    if (!s_in_parsing) {
        memset(s_input_buffer, 0, INPUT_BUFFER_SIZE);
        s_buffer_index = 0;
        s_in_parsing = true;
        INPUT_LOGI("Start receiving new frame...");
    }

    // 边界检查
    if (s_buffer_index >= INPUT_BUFFER_SIZE - 1) {
        INPUT_LOGI("Error: Buffer overflow!");
        s_in_parsing = false;
        s_buffer_index = 0;
        return;
    }

    // 存储字符
    s_input_buffer[s_buffer_index++] = c;
}

// 检查帧就绪标志
bool input_parser_frame_ready(void) {
    return s_frame_ready;
}

// 获取已就绪的帧
const char* input_parser_get_frame(void) {
    if (!s_frame_ready) {
        return NULL;
    }
    s_frame_ready = false; // 取走帧后清除标志
    return s_input_buffer;
}

// 初始化函数 (需指定模式)
void input_parser_init(bool timeout_enable, input_parser_mode_t mode) {
    s_timeout_enabled = timeout_enable;
    s_current_mode = mode;
    s_buffer_index = 0;
    s_frame_ready = false;
    s_in_parsing = false;

    if (mode == INPUT_MODE_NON_BLOCKING) {
        INPUT_LOGI("Non-blocking input parser ready. Call 'poll()' in main loop.");
    } else {
        INPUT_LOGI("Blocking input parser ready.");
    }
}

// 组帧核心函数
const char* input_parser_wait_and_parse(void) {
    if (s_current_mode == INPUT_MODE_NON_BLOCKING) {
        INPUT_LOGW("Warning: Called blocking function in non-blocking mode!");
        return NULL;
    }
    int c;
    int index = 0;
    
    // 清空缓冲区
    memset(s_input_buffer, 0, INPUT_BUFFER_SIZE);
    INPUT_LOGI("Waiting for input...");
    
    while (1) {
        c = getchar();
        
        if (c == EOF) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }
        
        // 回车键处理：结束组帧
        if (c == '\n' || c == '\r') {
            if (index == 0) {
                INPUT_LOGI("Received empty frame, ignoring.");
                return NULL;
            }
            s_input_buffer[index] = '\0';
            INPUT_LOGI("Input: %s", s_input_buffer);
            return s_input_buffer;
        }
        
        // 边界检查
        if (index >= INPUT_BUFFER_SIZE - 1) {
            INPUT_LOGI("Error: Exceeded buffer size, max length %d", INPUT_BUFFER_SIZE-1);
            memset(s_input_buffer, 0, INPUT_BUFFER_SIZE);
            return NULL;
        }
        
        // 存储字符
        s_input_buffer[index++] = c;
        
    }
}