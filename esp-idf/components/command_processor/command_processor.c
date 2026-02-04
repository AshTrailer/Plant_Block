#include "command_processor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char *TAG = "CMD_PROCESSOR";

#define MAX_MODULES 10
#define CONFIRM_TIMEOUT_TICKS (10000 / portTICK_PERIOD_MS)

typedef struct {
    char name[16];
    int status;
    int pin; // 新增：存储模块控制的引脚号，-1表示未设置
} module_status_t;

static module_status_t s_module_status[MAX_MODULES];
static int s_module_count = 0;
static bool s_confirmation_enabled = false;

static struct {
    bool is_waiting;
    char pending_name[16];
    int pending_status;
    TickType_t wait_start_ticks;
} s_confirm_state = {0};

// 查找模块索引
static int find_module_index(const char* name) {
    for (int i = 0; i < s_module_count; i++) {
        if (strcmp(s_module_status[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// 打印命令帮助
static void print_command_help(void) {
    ESP_LOGI(TAG, "=== Command Help ===");
    ESP_LOGI(TAG, "<module> 1/0        : Set module status (1=ON, 0=OFF)");
    ESP_LOGI(TAG, "<module> delete     : Delete the module from preset list");
    ESP_LOGI(TAG, "<module> pin <num>  : Set module control pin number(<=36)");
    ESP_LOGI(TAG, "<module> status     : Show module current status and pin");
    ESP_LOGI(TAG, "list                : List all preset modules");
    ESP_LOGI(TAG, "help                : Show this help message");
    ESP_LOGI(TAG, "=================");
}

// 初始化函数
void command_processor_init(bool enable_confirmation) {
    s_module_count = 0;
    s_confirmation_enabled = enable_confirmation;
    memset(&s_confirm_state, 0, sizeof(s_confirm_state));

    // 预置模块，引脚初始化为-1（未设置）
    strncpy(s_module_status[s_module_count].name, "fan", sizeof(s_module_status[0].name) - 1);
    s_module_status[s_module_count].status = 0;
    s_module_status[s_module_count].pin = -1;
    s_module_count++;

    strncpy(s_module_status[s_module_count].name, "pump", sizeof(s_module_status[0].name) - 1);
    s_module_status[s_module_count].status = 0;
    s_module_status[s_module_count].pin = -1;
    s_module_count++;

    ESP_LOGI(TAG, "Command processor ready. Pre-loaded %d module(s):", s_module_count);
    for (int i = 0; i < s_module_count; i++) { // 遍历打印
        ESP_LOGI(TAG, "  [%d] %s (status=%d, pin=%d)",
                 i, s_module_status[i].name, s_module_status[i].status, s_module_status[i].pin);
    }
    print_command_help(); // 初始化后显示帮助
}

void command_processor_poll_confirmation(void) {
    if (!s_confirm_state.is_waiting) return;
    if ((xTaskGetTickCount() - s_confirm_state.wait_start_ticks) > CONFIRM_TIMEOUT_TICKS) {
        ESP_LOGI(TAG, "Confirmation timeout for new module '%s'. Ignored.", s_confirm_state.pending_name);
        memset(&s_confirm_state, 0, sizeof(s_confirm_state));
    }
}

bool command_processor_is_waiting_for_confirm(void) {
    return s_confirm_state.is_waiting;
}

// 处理输入帧的核心函数
void command_processor_process_frame(const char* frame) {
    if (frame == NULL) return;

    ESP_LOGI(TAG, "Processing frame: %s", frame);

    // --- 处理确认响应 ---
    if (s_confirm_state.is_waiting) {
        char upper_frame[16];
        strncpy(upper_frame, frame, sizeof(upper_frame) - 1);
        upper_frame[sizeof(upper_frame)-1] = '\0';
        for (int i = 0; upper_frame[i]; i++) upper_frame[i] = toupper(upper_frame[i]);

        if (strcmp(upper_frame, "YES") == 0) {
            if (s_module_count >= MAX_MODULES) {
                ESP_LOGI(TAG, "Error: Module storage full. Cannot add '%s'.", s_confirm_state.pending_name);
            } else {
                strncpy(s_module_status[s_module_count].name, s_confirm_state.pending_name,
                        sizeof(s_module_status[0].name) - 1);
                s_module_status[s_module_count].status = s_confirm_state.pending_status;
                s_module_status[s_module_count].pin = -1; // 新增模块引脚默认为-1
                s_module_count++;
                ESP_LOGI(TAG, "New module '%s' added with status %d.", s_confirm_state.pending_name, s_confirm_state.pending_status);
            }
        } else if (strcmp(upper_frame, "NO") == 0) {
            ESP_LOGI(TAG, "Addition of new module '%s' cancelled.", s_confirm_state.pending_name);
        } else {
            ESP_LOGI(TAG, "Invalid confirmation response. Please enter 'YES' or 'NO'.");
            return;
        }
        memset(&s_confirm_state, 0, sizeof(s_confirm_state));
        return;
    }

    // --- 处理 help 命令 ---
    if (strcmp(frame, "help") == 0) {
        print_command_help();
        return;
    }

    // --- 处理 list 命令 ---
    if (strcmp(frame, "list") == 0) {
        if (s_module_count == 0) {
            ESP_LOGI(TAG, "No modules loaded.");
        } else {
            ESP_LOGI(TAG, "=== Module List (Total: %d) ===", s_module_count);
            ESP_LOGI(TAG, "Module     Status  Pin");
            ESP_LOGI(TAG, "-------    ------  ----");
            for (int i = 0; i < s_module_count; i++) {
                ESP_LOGI(TAG, "%-10s %-7d %d",
                         s_module_status[i].name,
                         s_module_status[i].status,
                         s_module_status[i].pin);
            }
            ESP_LOGI(TAG, "=============================");
        }
        return;
    }

    // ---解析通用命令格式 "<module> <action> [arg]" ---
    char module_name[16];
    char action[16];
    char arg[16];
    int num_matched = sscanf(frame, "%15s %15s %15s", module_name, action, arg);

    if (num_matched < 2) {
        ESP_LOGI(TAG, "Error: Invalid command format. Type 'help' for usage.");
        return;
    }

    int index = find_module_index(module_name);
    if (index == -1) {
        // 模块不存在，尝试按旧格式 "module status" 解析，触发确认流程
        char status_str[4];
        if (sscanf(frame, "%15s %3s", module_name, status_str) == 2 &&
            strlen(status_str) == 1 && (status_str[0] == '0' || status_str[0] == '1')) {
            int status_val = status_str[0] - '0';
            if (!s_confirmation_enabled) {
                ESP_LOGI(TAG, "Error: Module '%s' not found. (Auto-add disabled)", module_name);
                return;
            }
            strncpy(s_confirm_state.pending_name, module_name, sizeof(s_confirm_state.pending_name) - 1);
            s_confirm_state.pending_status = status_val;
            s_confirm_state.is_waiting = true;
            s_confirm_state.wait_start_ticks = xTaskGetTickCount();
            ESP_LOGI(TAG, "New module '%s' detected. Enter 'YES' to add (status=%d), or 'NO' to ignore.", module_name, status_val);
        } else {
            ESP_LOGI(TAG, "Error: Module '%s' not found.", module_name);
        }
        return;
    }

    // 模块存在，处理 action
    // 3.1 处理 delete 命令
    if (strcmp(action, "delete") == 0) {
        ESP_LOGI(TAG, "Deleting module '%s'...", module_name);
        // 将数组后续元素前移，覆盖要删除的模块
        for (int i = index; i < s_module_count - 1; i++) {
            s_module_status[i] = s_module_status[i + 1];
        }
        s_module_count--;
        ESP_LOGI(TAG, "Module '%s' deleted. Total modules: %d", module_name, s_module_count);
        return;
    }

    // 3.2 处理 pin 命令
    if (strcmp(action, "pin") == 0) {
        if (num_matched < 3) {
            ESP_LOGI(TAG, "Error: Pin number required. Usage: <module> pin <num>");
            return;
        }
        int pin_num = atoi(arg);
        if (pin_num < 0 || pin_num > 36) {
            ESP_LOGI(TAG, "Error: Pin number must be between 0 and 36.");
            return;
        }
        s_module_status[index].pin = pin_num;
        ESP_LOGI(TAG, "Module '%s' pin set to: %d", module_name, pin_num);
        return;
    }

    // 3.3 处理 status 命令
    if (strcmp(action, "status") == 0) {
        ESP_LOGI(TAG, "Module '%s' status: %d, pin: %d",
                 module_name, s_module_status[index].status, s_module_status[index].pin);
        return;
    }

    // 3.4 处理设置状态命令 (旧格式兼容：<module> <1/0>)
    if (strcmp(action, "1") == 0 || strcmp(action, "0") == 0) {
        int status_val = action[0] - '0';
        s_module_status[index].status = status_val;
        ESP_LOGI(TAG, "Module '%s' status updated to: %d", module_name, status_val);
        return;
    }

    // 未知的 action
    ESP_LOGI(TAG, "Error: Unknown action '%s' for module '%s'. Type 'help' for usage.", action, module_name);
}

// 查询模块状态
int command_processor_get_status(const char* module_name) {
    int index = find_module_index(module_name);
    return (index != -1) ? s_module_status[index].status : -1;
}

// 查询模块引脚
int command_processor_get_pin(const char* module_name) {
    int index = find_module_index(module_name);
    return (index != -1) ? s_module_status[index].pin : -1;
}