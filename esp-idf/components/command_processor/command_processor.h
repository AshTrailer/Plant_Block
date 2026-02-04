#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <stdbool.h>

// 初始化命令处理器模块，可预置模块
// 参数: enable_confirmation - 是否启用添加新模块的确认流程
void command_processor_init(bool enable_confirmation);

// 处理输入帧
void command_processor_process_frame(const char* frame);

// 供其他模块查询自身状态的函数
// 参数: module_name - 要查询的模块名 (如 "pump", "fan")
// 返回值: 该模块的当前状态 (1/0), 若模块不存在则返回 -1
int command_processor_get_status(const char* module_name);

// 内部确认流程状态机轮询函数 (需在主循环调用)
void command_processor_poll_confirmation(void);

// 检查确认流程是否正在进行中 (用于主程序判断)
bool command_processor_is_waiting_for_confirm(void);

#endif