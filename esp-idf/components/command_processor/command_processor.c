#include "command_processor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char *TAG = "CMD_PROCESSOR";

#define MAX_MODULES 10
#define CONFIRM_TIMEOUT_TICKS (10000 / portTICK_PERIOD_MS) // 10秒超时

typedef struct {
    char name[16];
    int status;
} module_status_t;

static module_status_t s_module_status[MAX_MODULES];
static int s_module_count = 0;
static bool s_confirmation_enabled = false;

// --- 新模块确认流程状态机变量 ---
static struct {
    bool is_waiting;
    char pending_name[16];
    int pending_status;
    TickType_t wait_start_ticks;
} s_confirm_state = {0};

// 查找模块索引，不存在返回-1
static int find_module_index(const char* name) {
    for (int i = 0; i < s_module_count; i++) {
        if (strcmp(s_module_status[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// 初始化函数：预置模块并配置确认功能
void command_processor_init(bool enable_confirmation) {
    s_module_count = 0;
    s_confirmation_enabled = enable_confirmation;
    memset(&s_confirm_state, 0, sizeof(s_confirm_state));

    // --- 预置您的模块列表 ---
    // 示例：预置 "fan" 和 "pump" 模块，状态初始化为0 (关闭)
    strncpy(s_module_status[s_module_count].name, "fan", sizeof(s_module_status[0].name) - 1);
    s_module_status[s_module_count].status = 0;
    s_module_count++;

    strncpy(s_module_status[s_module_count].name, "pump", sizeof(s_module_status[0].name) - 1);
    s_module_status[s_module_count].status = 0;
    s_module_count++;

    // 您可以继续添加其他预置模块...
    // strncpy(s_module_status[s_module_count].name, "light", ...);
    // s_module_count++;

    ESP_LOGI(TAG, "Command processor ready. Pre-loaded %d module(s).", s_module_count);
}

// 处理新模块确认流程的状态机轮询
void command_processor_poll_confirmation(void) {
    if (!s_confirm_state.is_waiting) {
        return;
    }

    // 检查超时 (30秒)
    if ((xTaskGetTickCount() - s_confirm_state.wait_start_ticks) > CONFIRM_TIMEOUT_TICKS) {
        ESP_LOGI(TAG, "Confirmation timeout for new module '%s'. Ignored.", s_confirm_state.pending_name);
        memset(&s_confirm_state, 0, sizeof(s_confirm_state));
        return;
    }

    // 注意：此处不主动读取输入，仅作为状态机维护。
    // 实际的“YES/NO”响应应由主程序通过 `command_processor_process_frame` 传入。
}

// 检查是否正在等待确认
bool command_processor_is_waiting_for_confirm(void) {
    return s_confirm_state.is_waiting;
}

// 处理输入帧的核心函数 (增强版)
void command_processor_process_frame(const char* frame) {
    if (frame == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Processing frame: %s", frame);

    // --- 情况1: 处理处于等待确认状态的响应 ("YES"/"NO") ---
    if (s_confirm_state.is_waiting) {
        // 转换为大写以便比较
        char upper_frame[16];
        strncpy(upper_frame, frame, sizeof(upper_frame) - 1);
        upper_frame[sizeof(upper_frame)-1] = '\0';
        for (int i = 0; upper_frame[i]; i++) {
            upper_frame[i] = toupper(upper_frame[i]);
        }

        if (strcmp(upper_frame, "YES") == 0) {
            // 确认添加
            if (s_module_count >= MAX_MODULES) {
                ESP_LOGI(TAG, "Error: Module storage full. Cannot add '%s'.", s_confirm_state.pending_name);
            } else {
                strncpy(s_module_status[s_module_count].name, s_confirm_state.pending_name,
                        sizeof(s_module_status[0].name) - 1);
                s_module_status[s_module_count].status = s_confirm_state.pending_status;
                s_module_count++;
                ESP_LOGI(TAG, "New module '%s' added with status %d.",
                         s_confirm_state.pending_name, s_confirm_state.pending_status);
            }
        } else if (strcmp(upper_frame, "NO") == 0) {
            // 拒绝添加
            ESP_LOGI(TAG, "Addition of new module '%s' cancelled.", s_confirm_state.pending_name);
        } else {
            ESP_LOGI(TAG, "Invalid confirmation response. Please enter 'YES' or 'NO'.");
            return; // 输入无效，保持等待状态
        }
        // 无论结果如何，结束确认等待状态
        memset(&s_confirm_state, 0, sizeof(s_confirm_state));
        return;
    }

    // --- 情况2: 正常解析命令帧 "module status" ---
    char module_name[16];
    char status_str[4];
    int status_val;

    if (sscanf(frame, "%15s %3s", module_name, status_str) != 2) {
        ESP_LOGI(TAG, "Error: Invalid format. Expected 'MODULE_NAME 1/0'.");
        return;
    }

    if (strlen(status_str) != 1 || (status_str[0] != '0' && status_str[0] != '1')) {
        ESP_LOGI(TAG, "Error: Status must be 0 or 1.");
        return;
    }
    status_val = status_str[0] - '0';

    int index = find_module_index(module_name);
    if (index != -1) {
        // 模块已存在，直接更新状态
        s_module_status[index].status = status_val;
        ESP_LOGI(TAG, "Module '%s' status updated to: %d", module_name, status_val);
        return;
    }

    // --- 模块不存在，进入确认流程 ---
    if (!s_confirmation_enabled) {
        // 未启用确认功能，直接拒绝
        ESP_LOGI(TAG, "Error: Module '%s' not found. (Auto-add disabled)", module_name);
        return;
    }

    // 启用确认功能，记录待添加模块并进入等待状态
    strncpy(s_confirm_state.pending_name, module_name, sizeof(s_confirm_state.pending_name) - 1);
    s_confirm_state.pending_status = status_val;
    s_confirm_state.is_waiting = true;
    s_confirm_state.wait_start_ticks = xTaskGetTickCount();

    ESP_LOGI(TAG, "New module '%s' detected. Enter 'YES' to add (status=%d), or 'NO' to ignore.",
             module_name, status_val);
}

// 供其他模块查询自身状态的函数 (保持不变)
int command_processor_get_status(const char* module_name) {
    int index = find_module_index(module_name);
    if (index != -1) {
        return s_module_status[index].status;
    }
    ESP_LOGW(TAG, "Module '%s' not found.", module_name);
    return -1;
}

