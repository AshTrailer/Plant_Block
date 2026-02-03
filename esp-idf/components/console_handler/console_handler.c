#include "console_handler.h"
#include "fan_controller.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>  // 添加此头文件以支持PRIu32

static const char *TAG = "CONSOLE_HANDLER";

// 先定义print_status函数，这样它可以在execute_command中被调用
void console_handler_print_status(void) {
    bool fan_state = fan_controller_get_state();
    
    printf("\n=== System Status ===\n");
    printf("Fan State: %s\n", fan_state ? "ON" : "OFF");
    printf("GPIO Pin: %d\n", FAN_GPIO_PIN);
    printf("Free Heap: %"PRIu32" bytes\n", esp_get_free_heap_size());  // 修正格式字符串
    printf("=====================\n\n");
}

void console_handler_execute_command(parsed_command_t *cmd) {
    if (!cmd) return;

    switch (cmd->type) {
        case CMD_FAN:
            if (cmd->arg_count == 2) {
                if (strcmp(cmd->args[1], "1") == 0 || strcmp(cmd->args[1], "on") == 0) {
                    if (fan_controller_set_state(true)) {
                        printf("Fan started successfully\n");
                    } else {
                        printf("Failed to start fan\n");
                    }
                } else if (strcmp(cmd->args[1], "0") == 0 || strcmp(cmd->args[1], "off") == 0) {
                    if (fan_controller_set_state(false)) {
                        printf("Fan stopped successfully\n");
                    } else {
                        printf("Failed to stop fan\n");
                    }
                } else {
                    printf("Invalid argument for fan command. Use 'fan 0' or 'fan 1'\n");
                }
            } else {
                printf("Usage: fan <0|1>\n");
            }
            break;

        case CMD_STATUS:
            console_handler_print_status();
            break;

        case CMD_HELP:
            command_parser_print_help();
            break;

        case CMD_VERSION:
            printf("Plant Monitoring System v1.0\n");
            printf("Compiled on: %s %s\n", __DATE__, __TIME__);
            break;

        case CMD_REBOOT:
            printf("Rebooting system...\n");
            esp_restart();
            break;

        case CMD_UNKNOWN:
            printf("Unknown command: %s\n", cmd->args[0]);
            printf("Type 'help' for available commands\n");
            break;
    }
}

void console_handler_init(void) {
    ESP_LOGI(TAG, "Console handler initialized");
    printf("\n");
    printf("================================\n");
    printf("   ESP32-C6 Fan Control System  \n");
    printf("================================\n");
    printf("Type 'help' for available commands\n\n");
}