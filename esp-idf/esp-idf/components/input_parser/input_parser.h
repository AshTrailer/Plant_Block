#ifndef INPUT_PARSER_H
#define INPUT_PARSER_H

#include <stdbool.h>

// 模块工作模式枚举
typedef enum {
    INPUT_MODE_BLOCKING,    // 阻塞模式 (保持旧版兼容)
    INPUT_MODE_NON_BLOCKING // 非阻塞模式 (推荐新架构使用)
} input_parser_mode_t;

// 初始化，可指定模式
void input_parser_init(bool timeout_enable, input_parser_mode_t mode);

// --- 非阻塞模式专用API ---
// 轮询函数：必须在主循环中定期调用
void input_parser_poll(void);
// 检查是否有新帧就绪
bool input_parser_frame_ready(void);
// 获取就绪的帧，获取后内部帧标记为已取走
const char* input_parser_get_frame(void);

// --- 阻塞模式专用API (兼容旧版) ---
// 注意：在非阻塞模式下调用此函数将产生警告并返回NULL
const char* input_parser_wait_and_parse(void);

#endif