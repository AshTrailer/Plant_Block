#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>

// 命令处理回调：argc=参数个数, argv[0]=命令名, argv[1..]=参数
typedef void (*cmd_handler_t)(int argc, char *argv[]);

// 命令表条目
typedef struct {
   const char    *name;       // 命令名（如 "light", "time"）
   cmd_handler_t  handler;    // 处理函数
   const char    *usage;      // 用法示例（如 "light set start 08:00"）
   const char    *help;       // 帮助说明
} cmd_entry_t;

void command_processor_init(void);
void command_processor_process_frame(const char *frame);

// 获取命令表（用于 help 命令遍历）
const cmd_entry_t *command_processor_get_table(void);
int command_processor_get_table_size(void);

#endif // COMMAND_PROCESSOR_H