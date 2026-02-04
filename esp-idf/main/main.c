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
    command_processor_init();
    
    const char* frame = input_parser_wait_and_parse();
    
    while (1)
    {
       input_parser_poll(); // 此函数调用极快，不会阻塞
        
        // 检查是否有新帧到达
        if (input_parser_frame_ready()) {
            const char* frame = input_parser_get_frame();
            if (frame != NULL) {
                ESP_LOGI("MAIN", "New frame: %s", frame);
                command_processor_process_frame(frame);
                
                // 示例查询
                int fan_status = command_processor_get_status("fan");
                ESP_LOGI("MAIN", "Fan status: %d", fan_status);
            }
        }
        
        // --- 此处可安全添加其他任务代码 ---
        // 例如：传感器读取、状态机更新、LED闪烁等
        // 这些代码将能与输入处理并发运行
        
        // 控制主循环频率，防止看门狗，也减少CPU占用
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
          
}