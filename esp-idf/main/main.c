#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "input_parser.h"
#include "command_processor.h"
#include "gpio_control.h"

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);
    
    input_parser_init(false, INPUT_MODE_NON_BLOCKING);
    command_processor_init(true);

    gpio_control_init();

    while (1) {
        // 轮询输入
        input_parser_poll();

        // 轮询命令处理器的确认状态机
        command_processor_poll_confirmation();

        // 检查是否有新帧到达
        if (input_parser_frame_ready()) {
            const char* frame = input_parser_get_frame();
            if (frame != NULL) {
                ESP_LOGI("MAIN", "收到命令: %s", frame);
                
                // 检查是否为GPIO命令
                if (strncmp(frame, "gpio", 4) == 0) {
                    // 复制命令字符串用于解析（strtok会修改原字符串）
                    char cmd_copy[32];
                    strncpy(cmd_copy, frame, sizeof(cmd_copy) - 1);
                    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
                    
                    // 解析命令格式: gpio <pin> <level>
                    char* token = strtok(cmd_copy, " ");
                    
                    // 第一个token应该是"gpio"
                    if (token == NULL || strcmp(token, "gpio") != 0) {
                        ESP_LOGI("MAIN", "错误: 命令格式错误");
                        continue;
                    }
                    
                    // 获取引脚号
                    token = strtok(NULL, " ");
                    if (token == NULL) {
                        ESP_LOGI("MAIN", "错误: 缺少引脚号");
                        ESP_LOGI("MAIN", "用法: gpio <pin> <1/0>");
                        continue;
                    }
                    
                    int pin_num = atoi(token);
                    
                    // 获取电平值
                    token = strtok(NULL, " ");
                    if (token == NULL) {
                        ESP_LOGI("MAIN", "错误: 缺少电平值");
                        ESP_LOGI("MAIN", "用法: gpio <pin> <1/0>");
                        continue;
                    }
                    
                    bool level;
                    if (strcmp(token, "1") == 0) {
                        level = true;
                    } else if (strcmp(token, "0") == 0) {
                        level = false;
                    } else {
                        ESP_LOGI("MAIN", "错误: 电平值必须是 1 或 0，收到: %s", token);
                        continue;
                    }
                    
                    // 调用GPIO控制函数
                    ESP_LOGI("MAIN", "执行: GPIO%d -> %s", pin_num, level ? "高电平" : "低电平");
                    bool success = gpio_control_set_level(pin_num, level);
                    
                    if (!success) {
                        ESP_LOGI("MAIN", "GPIO控制失败，请检查引脚号是否在管理列表中");
                    }
                } else {
                    // 非GPIO命令，交给命令处理器处理
                    command_processor_process_frame(frame);
                }
            }
        }

        // 主循环延时
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
          
}