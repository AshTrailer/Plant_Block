#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <stdbool.h>

#define MAX_COMMAND_LENGTH  64
#define MAX_TOKEN_COUNT     10

typedef enum {
    CMD_FAN,
    CMD_HELP,
    CMD_STATUS,
    CMD_VERSION,
    CMD_REBOOT,
    CMD_UNKNOWN
} command_type_t;

typedef struct {
    command_type_t type;
    char args[MAX_TOKEN_COUNT][32];
    int arg_count;
} parsed_command_t;

/**
 * @brief 解析命令行输入
 * @param input 输入字符串
 * @param cmd 解析后的命令结构体
 * @return true: 解析成功, false: 解析失败
 */
bool command_parser_parse(const char *input, parsed_command_t *cmd);

/**
 * @brief 打印可用命令帮助信息
 */
void command_parser_print_help(void);

#endif // COMMAND_PARSER_H