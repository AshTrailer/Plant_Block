#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "startup_logger.h" // 包含新模块

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);
    
    // 调用模块的打印函数
    startup_logger_print();
    
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI("MAIN", "系统运行中...");
    }
}