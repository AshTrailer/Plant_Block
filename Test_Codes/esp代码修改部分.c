#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// ... (保留原有的 include)
#include "moisture_sensor.h"

// 新增标准库头文件
#include <string.h>
#include <stdlib.h>

void app_main(void) {
    // ... (初始化代码完全保持不变)
    ESP_LOGI("MAIN", "All components initialized.");

    while (1) {
        // 轮询输入
        input_parser_poll();
        light_control_poll();
        irrigation_controller_poll();

        if (input_parser_frame_ready()) {
            const char* frame = input_parser_get_frame();
            if (frame != NULL) {
                // --- 修改开始：拦截屏幕数据 ---
                if (strncmp(frame, "SET_N0:", 7) == 0) {
                    // 解析冒号后的数字
                    int val = atoi(frame + 7);
                    ESP_LOGI("HMI", "Received n0 value: %d", val);
                    
                    // TODO: 在这里添加你的业务逻辑，比如：
                    // ventilation_control_set_threshold(val); 
                } 
                else {
                    // 如果不是屏幕指令，则交给原有命令处理器
                    command_processor_process_frame(frame);
                }
                // --- 修改结束 ---
            }
        }

        // 主循环延时
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
