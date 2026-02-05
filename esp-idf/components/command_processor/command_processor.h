#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <stdbool.h>

// 初始化命令处理器模块，可预置模块
// 参数: enable_confirmation - 是否启用添加新模块的确认流程
void command_processor_init(bool enable_confirmation);

// 处理所有输入帧（包括light、time和其他命令）
void command_processor_process_frame(const char* frame);

// 供其他模块查询自身状态的函数
int command_processor_get_status(const char* module_name);

// 供其他模块查询自身引脚配置的函数
int command_processor_get_pin(const char* module_name);

// 内部确认流程状态机轮询函数 (需在主循环调用)
void command_processor_poll_confirmation(void);

// 检查确认流程是否正在进行中 (用于主程序判断)
bool command_processor_is_waiting_for_confirm(void);

#endif