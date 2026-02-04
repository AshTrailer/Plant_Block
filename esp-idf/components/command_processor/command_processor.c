#include "command_processor.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>


static const char *TAG = "CMD_PROCESSOR";

// 定义最大可存储的模块数量
#define MAX_MODULES 10

// 模块状态存储结构体
typedef struct {
    char name[16];  // 模块名
    int status;     // 模块状态 (1或0)
} module_status_t;

// 模块状态存储数组
static module_status_t s_module_status[MAX_MODULES];
static int s_module_count = 0;

// 初始化函数
void command_processor_init(void) {
    // 清空状态存储
    for (int i = 0; i < MAX_MODULES; i++) {
        s_module_status[i].name[0] = '\0';
        s_module_status[i].status = -1; // -1表示未定义
    }
    s_module_count = 0;
    ESP_LOGI(TAG, "Command processor ready.");
}

// 处理输入帧的核心函数
void command_processor_process_frame(const char* frame) {
    if (frame == NULL) {
        ESP_LOGI(TAG, "Received NULL frame, ignoring.");
        return;
    }

    ESP_LOGI(TAG, "Processing frame: %s", frame);

    char module_name[16];
    char status_str[4];
    int status_val;

    // 尝试解析格式 "module status"
    if (sscanf(frame, "%15s %3s", module_name, status_str) != 2) {
        ESP_LOGI(TAG, "Error: Invalid format. Expected 'MODULE_NAME 1/0'.");
        return;
    }

    // 检查状态值是否为合法数字 (0 或 1)
    if (strlen(status_str) != 1 || (status_str[0] != '0' && status_str[0] != '1')) {
        ESP_LOGI(TAG, "Error: Status must be 0 or 1.");
        return;
    }
    status_val = status_str[0] - '0'; // 转换为整数

    // 查找或创建模块条目
    int index = -1;
    for (int i = 0; i < s_module_count; i++) {
        if (strcmp(s_module_status[i].name, module_name) == 0) {
            index = i;
            break;
        }
    }

    // 如果模块不存在且数组未满，则创建新条目
    if (index == -1) {
        if (s_module_count >= MAX_MODULES) {
            ESP_LOGI(TAG, "Error: Module storage full (max %d).", MAX_MODULES);
            return;
        }
        index = s_module_count;
        strncpy(s_module_status[index].name, module_name, sizeof(s_module_status[index].name) - 1);
        s_module_status[index].name[sizeof(s_module_status[index].name) - 1] = '\0';
        s_module_count++;
    }

    // 更新模块状态
    s_module_status[index].status = status_val;
    ESP_LOGI(TAG, "Module '%s' status updated to: %d", module_name, status_val);
}

// 供其他模块查询自身状态的函数
int command_processor_get_status(const char* module_name) {
    for (int i = 0; i < s_module_count; i++) {
        if (strcmp(s_module_status[i].name, module_name) == 0) {
            return s_module_status[i].status;
        }
    }
    // 模块不存在
    ESP_LOGW(TAG, "Module '%s' not found.", module_name);
    return -1;
}