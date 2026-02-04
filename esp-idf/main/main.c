#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "startup_logger.h"
#include "input_parser.h"
#include "command_processor.h"

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);
    
    startup_logger_print();
    input_parser_init(false, INPUT_MODE_NON_BLOCKING);
    command_processor_init(true);
        
    while (1) {
        // 1. 轮询输入
        input_parser_poll();

        // 2. 轮询命令处理器的确认状态机
        command_processor_poll_confirmation();

        // 3. 检查并处理新帧
        if (input_parser_frame_ready()) {
            const char* frame = input_parser_get_frame();
            if (frame != NULL) {
                ESP_LOGI("MAIN", "New frame: %s", frame);

                // 如果当前正在等待确认，则将帧直接交给确认流程处理
                // 否则，按正常命令处理
                command_processor_process_frame(frame);
            }
        }

        // 4. 示例：查询预置模块状态
        int fan_status = command_processor_get_status("fan");
        int pump_status = command_processor_get_status("pump");
        // 可以根据状态执行实际控制...

        // 5. 主循环延时
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
          
}