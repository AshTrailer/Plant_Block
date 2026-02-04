#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <stdbool.h>

// 初始化命令处理器模块
void command_processor_init(void);

// 处理输入帧
void command_processor_process_frame(const char* frame);

// 供其他模块查询自身状态的函数
// 参数: module_name - 要查询的模块名 (如 "pump", "fan")
// 返回值: 该模块的当前状态 (1/0), 若模块不存在则返回 -1
int command_processor_get_status(const char* module_name);

#endif