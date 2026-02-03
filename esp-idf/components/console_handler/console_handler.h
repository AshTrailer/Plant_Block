#ifndef CONSOLE_HANDLER_H
#define CONSOLE_HANDLER_H

#include "command_parser.h"

/**
 * @brief 初始化控制台处理
 */
void console_handler_init(void);

/**
 * @brief 处理解析后的命令
 * @param cmd 解析后的命令
 */
void console_handler_execute_command(parsed_command_t *cmd);

/**
 * @brief 打印系统状态
 */
void console_handler_print_status(void);

#endif // CONSOLE_HANDLER_H