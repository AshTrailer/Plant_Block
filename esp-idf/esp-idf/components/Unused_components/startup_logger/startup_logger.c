#include "startup_logger.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void startup_logger_print(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    极简测试程序启动成功！");
    ESP_LOGI(TAG, "    所有外设均未初始化。");
    ESP_LOGI(TAG, "========================================");
}