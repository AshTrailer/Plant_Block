#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "time_manager.h"
#include "input_parser.h"
#include "command_processor.h"
#include "gpio_control.h"
#include "ventilation_control.h"

void app_main(void) {
    ESP_LOGI("MAIN", "System starting, initializing components...");
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI("MAIN", "Initializing Time Manager...");
    time_manager_init();

    ESP_LOGI("MAIN", "Initializing GPIO Control...");
    gpio_control_init();

    ESP_LOGI("MAIN", "Initializing Input Parser...");
    input_parser_init(false, INPUT_MODE_NON_BLOCKING);

    ESP_LOGI("MAIN", "Initializing Command Processor...");
    command_processor_init(true);

    ESP_LOGI("MAIN", "Initializing Ventilation Control...");
    ventilation_control_init(1);
    ventilation_control_start();

    ESP_LOGI("MAIN", "All components initialized.");

    while (1) {
        // 轮询输入
        input_parser_poll();

        // 轮询命令处理器的确认状态机
        command_processor_poll_confirmation();

        // 检查是否有新帧到达
        if (input_parser_frame_ready()) {
            const char* frame = input_parser_get_frame();
            if (frame != NULL) {
                ESP_LOGI("MAIN", "Received: %s", frame);

                if (strncmp(frame, "time", 4) == 0) {
                    // 时间相关命令交给时间管理器处理
                    time_manager_process_command(frame);
                } else {
                    // 其他命令交给命令处理器
                    command_processor_process_frame(frame);
                }
            }
        }

        // 主循环延时
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
          
}