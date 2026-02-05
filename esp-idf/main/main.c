#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "time_manager.h"
#include "input_parser.h"
#include "command_processor.h"
#include "gpio_control.h"
#include "ventilation_control.h"
#include "light_control.h"
#include "pwm_test.h"

void app_main(void) {
    ESP_LOGI("MAIN", "System starting, initializing components...");
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI("MAIN", "Initializing GPIO Control...");
    gpio_control_init();

    ESP_LOGI("MAIN", "Initializing Time Manager...");
    time_manager_init();    

    ESP_LOGI("MAIN", "Initializing Input Parser...");
    input_parser_init(false, INPUT_MODE_NON_BLOCKING);

    ESP_LOGI("MAIN", "Initializing Command Processor...");
    command_processor_init();

    ESP_LOGI("MAIN", "Initializing Ventilation Control...");
    ventilation_control_init(1);
    ventilation_control_start();

    ESP_LOGI("MAIN", "Initializing Light Control...");
    light_control_init(15);

    ESP_LOGI("MAIN", "Initializing PWM Test...");
    pwm_test_init(14);

    ESP_LOGI("MAIN", "All components initialized.");

    while (1) {
        // 轮询输入
        input_parser_poll();
        light_control_poll();

        if (input_parser_frame_ready()) {
            const char* frame = input_parser_get_frame();
            if (frame != NULL) {

                //command_processor_process_frame(frame);

                if (strncmp(frame, "pwm", 3) == 0) {
                    pwm_test_process_command(frame);
                }else {
                    command_processor_process_frame(frame);
                }
            }
        }


        // 主循环延时
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
          
}