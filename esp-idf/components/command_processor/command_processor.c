#include "command_processor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "light_control.h"
#include "time_manager.h"
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

// 打印完整命令帮助（整合所有模块）
static void print_command_help(void) {
    ESP_LOGI(TAG, "=== 完整命令帮助 ===");
    
    // 系统命令部分
    ESP_LOGI(TAG, "--- 系统命令 ---");
    ESP_LOGI(TAG, "help               - 显示此帮助");
    ESP_LOGI(TAG, "list               - 列出所有控制模块");
    
    // 模块管理部分
    ESP_LOGI(TAG, "--- 模块管理 ---");
    ESP_LOGI(TAG, "<module> 1/0        : 设置模块状态 (1=ON, 0=OFF)");
    ESP_LOGI(TAG, "<module> delete     : 从预设列表中删除模块");
    ESP_LOGI(TAG, "<module> pin <num>  : 设置模块控制引脚号 (0-36)");
    ESP_LOGI(TAG, "<module> status     : 显示模块当前状态和引脚");
    
    // 补光灯控制部分
    ESP_LOGI(TAG, "--- 补光灯控制 ---");
    ESP_LOGI(TAG, "light set start HH:MM  - 设置开启时间");
    ESP_LOGI(TAG, "light set end HH:MM    - 设置关闭时间");
    ESP_LOGI(TAG, "light set duration X.X - 设置照明时长(小时)");
    ESP_LOGI(TAG, "light on               - 手动开启");
    ESP_LOGI(TAG, "light off              - 手动关闭");
    ESP_LOGI(TAG, "light auto             - 切换为自动模式");
    ESP_LOGI(TAG, "light status           - 显示状态");
    ESP_LOGI(TAG, "light help             - 显示补光灯帮助");
    
    // 时间管理部分
    ESP_LOGI(TAG, "--- 时间管理 ---");
    ESP_LOGI(TAG, "time                  - 显示当前时间");
    ESP_LOGI(TAG, "time get              - 显示当前时间");
    ESP_LOGI(TAG, "time set Y/M/D H:M:S  - 设置系统时间");
    ESP_LOGI(TAG, "time help             - 显示时间帮助");
    
    // 示例部分
    ESP_LOGI(TAG, "--- 示例 ---");
    ESP_LOGI(TAG, "模块管理: fan 1, pump pin 13, fan status");
    ESP_LOGI(TAG, "补光灯: light set start 08:30, light auto");
    ESP_LOGI(TAG, "时间: time set 2025/02/05 14:30:00");
    ESP_LOGI(TAG, "=========================");
}

// 处理补光灯命令（从light_control_process_command移入）
static void handle_light_command(const char* command) {
    if (strncmp(command, "light set start ", 16) == 0) {
        // 格式: light set start HH:MM
        int hour, minute;
        if (sscanf(command + 16, "%d:%d", &hour, &minute) == 2) {
            if (light_control_set_start_time(hour, minute)) {
                ESP_LOGI(TAG, "补光灯开启时间设置成功");
            }
        } else {
            ESP_LOGI(TAG, "格式错误，正确格式: light set start HH:MM");
        }
    }
    else if (strncmp(command, "light set end ", 14) == 0) {
        // 格式: light set end HH:MM
        int hour, minute;
        if (sscanf(command + 14, "%d:%d", &hour, &minute) == 2) {
            if (light_control_set_end_time(hour, minute)) {
                ESP_LOGI(TAG, "补光灯关闭时间设置成功");
            }
        } else {
            ESP_LOGI(TAG, "格式错误，正确格式: light set end HH:MM");
        }
    }
    else if (strncmp(command, "light set duration ", 19) == 0) {
        // 格式: light set duration X.X
        float hours;
        if (sscanf(command + 19, "%f", &hours) == 1) {
            if (light_control_set_duration(hours)) {
                ESP_LOGI(TAG, "补光灯照明时长设置成功");
            }
        } else {
            ESP_LOGI(TAG, "格式错误，正确格式: light set duration X.X");
        }
    }
    else if (strcmp(command, "light on") == 0) {
        // 手动开启
        light_control_manual_set(true);
    }
    else if (strcmp(command, "light off") == 0) {
        // 手动关闭
        light_control_manual_set(false);
    }
    else if (strcmp(command, "light auto") == 0) {
        // 切回自动模式
        light_control_set_auto_mode();
    }
    else if (strcmp(command, "light status") == 0) {
        int start_hour = light_control_get_start_hour();
        int start_minute = light_control_get_start_minute();
        int end_hour = light_control_get_end_hour();
        int end_minute = light_control_get_end_minute();
        float duration = light_control_get_duration();
        // 显示状态
        ESP_LOGI(TAG, "=== 补光灯状态 ===");
        ESP_LOGI(TAG, "开启时间: %02d:%02d", start_hour, start_minute);
        ESP_LOGI(TAG, "关闭时间: %02d:%02d", end_hour, end_minute);
        ESP_LOGI(TAG, "照明时长: %.1f小时", duration);
        ESP_LOGI(TAG, "补光灯状态: %s", light_control_is_on() ? "开启" : "关闭");
        ESP_LOGI(TAG, "模式: %s", light_control_is_manual_mode() ? "手动" : "自动");
        ESP_LOGI(TAG, "================");
    }
    else if (strcmp(command, "light help") == 0) {
        // 只显示补光灯部分帮助
        ESP_LOGI(TAG, "=== 补光灯命令帮助 ===");
        ESP_LOGI(TAG, "light set start HH:MM  - 设置开启时间");
        ESP_LOGI(TAG, "light set end HH:MM    - 设置关闭时间");
        ESP_LOGI(TAG, "light set duration X.X - 设置照明时长(小时)");
        ESP_LOGI(TAG, "light on               - 手动开启");
        ESP_LOGI(TAG, "light off              - 手动关闭");
        ESP_LOGI(TAG, "light auto             - 切换为自动模式");
        ESP_LOGI(TAG, "light status           - 显示状态");
        ESP_LOGI(TAG, "=====================");
    }
    else {
        ESP_LOGI(TAG, "未知的补光灯命令，输入 'light help' 查看帮助");
    }
}

// 处理时间命令（从time_manager_process_command移入）
static void handle_time_command(const char* command) {
    // 检查是否为时间设置命令
    if (strncmp(command, "time set ", 9) == 0) {
        int year, month, day, hour, minute, second;
        
        // 解析格式: time set YYYY/MM/DD HH:MM:SS
        int result = sscanf(command + 9, "%d/%d/%d %d:%d:%d",
                           &year, &month, &day, &hour, &minute, &second);
        
        if (result == 6) {
            if (time_manager_set_time(year, month, day, hour, minute, second)) {
                ESP_LOGI(TAG, "时间设置成功: %s", time_manager_get_time_string());
            } else {
                ESP_LOGI(TAG, "时间设置失败，请检查参数");
            }
        } else {
            ESP_LOGI(TAG, "时间格式错误");
            ESP_LOGI(TAG, "正确格式: time set YYYY/MM/DD HH:MM:SS");
            ESP_LOGI(TAG, "示例: time set 2025/02/05 14:30:00");
        }
    } else if (strcmp(command, "time") == 0 || strcmp(command, "time get") == 0) {
        // 查询当前时间
        ESP_LOGI(TAG, "当前时间: %s", time_manager_get_time_string());
    } else if (strcmp(command, "time help") == 0) {
        // 显示时间帮助
        ESP_LOGI(TAG, "=== 时间命令帮助 ===");
        ESP_LOGI(TAG, "time                  - 显示当前时间");
        ESP_LOGI(TAG, "time get              - 显示当前时间");
        ESP_LOGI(TAG, "time set Y/M/D H:M:S  - 设置系统时间");
        ESP_LOGI(TAG, "===================");
    } else {
        ESP_LOGI(TAG, "未知的时间命令，输入 'time help' 查看帮助");
    }
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

    // --- 处理补光灯命令 ---
    if (strncmp(frame, "light", 5) == 0) {
        handle_light_command(frame);
        return;
    }

    // --- 处理时间命令 ---
    if (strncmp(frame, "time", 4) == 0) {
        handle_time_command(frame);
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