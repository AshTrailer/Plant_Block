#include "command_parser.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"

static const char *TAG = "CMD_PARSER";

static void string_to_lower(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

static void trim_whitespace(char *str) {
    char *end;
    
    // 去除开头空白
    while (isspace((unsigned char)*str)) str++;
    
    // 去除结尾空白
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    *(end + 1) = '\0';
}

bool command_parser_parse(const char *input, parsed_command_t *cmd) {
    if (!input || !cmd) {
        ESP_LOGE(TAG, "Invalid input or command pointer");
        return false;
    }

    char buffer[MAX_COMMAND_LENGTH];
    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    trim_whitespace(buffer);
    if (strlen(buffer) == 0) {
        ESP_LOGD(TAG, "Empty command");
        return false;
    }

    // 分词
    char *token;
    char *rest = buffer;
    cmd->arg_count = 0;
    
    while ((token = strtok_r(rest, " ", &rest)) && 
           cmd->arg_count < MAX_TOKEN_COUNT) {
        trim_whitespace(token);
        if (strlen(token) > 0) {
            strncpy(cmd->args[cmd->arg_count], token, 
                   sizeof(cmd->args[cmd->arg_count]) - 1);
            cmd->args[cmd->arg_count][sizeof(cmd->args[cmd->arg_count]) - 1] = '\0';
            cmd->arg_count++;
        }
    }

    if (cmd->arg_count == 0) {
        ESP_LOGW(TAG, "No tokens found in command");
        return false;
    }

    // 识别命令类型
    string_to_lower(cmd->args[0]);
    
    if (strcmp(cmd->args[0], "fan") == 0) {
        cmd->type = CMD_FAN;
    } else if (strcmp(cmd->args[0], "help") == 0) {
        cmd->type = CMD_HELP;
    } else if (strcmp(cmd->args[0], "status") == 0) {
        cmd->type = CMD_STATUS;
    } else if (strcmp(cmd->args[0], "version") == 0 || strcmp(cmd->args[0], "ver") == 0) {
        cmd->type = CMD_VERSION;
    } else if (strcmp(cmd->args[0], "reboot") == 0 || strcmp(cmd->args[0], "reset") == 0) {
        cmd->type = CMD_REBOOT;
    } else {
        cmd->type = CMD_UNKNOWN;
    }

    ESP_LOGD(TAG, "Parsed command: type=%d, args=%d", cmd->type, cmd->arg_count);
    return true;
}

void command_parser_print_help(void) {
    printf("\n=== Available Commands ===\n");
    printf("fan <0|1|on|off>   - Control fan (0=off, 1=on)\n");
    printf("status              - Show system status\n");
    printf("version             - Show version information\n");
    printf("reboot              - Reboot the system\n");
    printf("help                - Show this help message\n");
    printf("============================\n\n");
}