#include "gpio_test.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "GPIO_TEST";

// 初始化并设置指定GPIO状态
void gpio_test_init(void) {
    esp_err_t ret;
    
    // 1. 定义要配置的GPIO引脚
    const int output_pins[] = {2, 3, 8, 9};
    const int pin_count = sizeof(output_pins) / sizeof(output_pins[0]);
    
    // 2. 构建引脚位掩码
    uint64_t pin_bit_mask = 0;
    for (int i = 0; i < pin_count; i++) {
        pin_bit_mask |= (1ULL << output_pins[i]);
    }
    
    // 3. 配置GPIO为输出模式
    gpio_config_t io_conf = {
        .pin_bit_mask = pin_bit_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO配置失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 4. 设置GPIO2和GPIO3为低电平
    gpio_set_level(GPIO_NUM_2, 0);
    gpio_set_level(GPIO_NUM_3, 0);
    
    // 5. 设置GPIO8和GPIO9为高电平
    gpio_set_level(GPIO_NUM_8, 1);
    gpio_set_level(GPIO_NUM_9, 1);
    
    ESP_LOGI(TAG, "GPIO测试模块初始化完成");
    ESP_LOGI(TAG, "GPIO2, GPIO3: 低电平 | GPIO8, GPIO9: 高电平");
}

// 反初始化，重置GPIO（可选）
void gpio_test_deinit(void) {
    // 将所有测试引脚重置为默认状态
    gpio_reset_pin(GPIO_NUM_2);
    gpio_reset_pin(GPIO_NUM_3);
    gpio_reset_pin(GPIO_NUM_8);
    gpio_reset_pin(GPIO_NUM_9);
    
    ESP_LOGI(TAG, "GPIO测试模块反初始化完成");
}