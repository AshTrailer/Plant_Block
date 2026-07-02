#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <stdbool.h>

// 初始化命令处理器模块
void command_processor_init(void);

// 处理所有输入帧（包括light、time和其他命令）
void command_processor_process_frame(const char* frame);

#endif