#include "gpio_test.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "GPIO_TEST";

// 初始化并设置指定GPIO状态
void gpio_test_init(void) {
    esp_err_t ret;
    
    // 定义需要设置为低电平的所有GPIO引脚
    const int low_pins[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 14, 15, 18};
    const int pin_count = sizeof(low_pins) / sizeof(low_pins[0]);
    
    ESP_LOGI(TAG, "开始配置 %d 个GPIO引脚为低电平...", pin_count);
    
    // 1. 对每个引脚进行单独、精确的配置
    for (int i = 0; i < pin_count; i++) {
        gpio_num_t pin_num = low_pins[i];
        
        // 关键步骤：先重置引脚，解除所有可能存在的功能复用（如SPI、SDIO、JTAG）
        gpio_reset_pin(pin_num);
        
        // 配置为输出模式
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << pin_num),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "引脚 GPIO%d 配置失败: %s", pin_num, esp_err_to_name(ret));
            // 继续配置其他引脚，不返回
            continue;
        }
        
        // 设置为低电平
        gpio_set_level(pin_num, 0);
        
        ESP_LOGI(TAG, "引脚 GPIO%d 已配置为低电平输出", pin_num);
    }    
    ESP_LOGI(TAG, "GPIO测试模块初始化完成");

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


void gpio_control_process_frame(const char* frame) {
    if (frame == NULL) {
        return;
    }

    // 检查是否为GPIO命令
    if (strncmp(frame, "gpio", 4) != 0) {
        // 不是GPIO命令，忽略
        return;
    }

    char cmd[32];
    strncpy(cmd, frame, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';

    ESP_LOGI(TAG, "解析命令: %s", cmd);

    // 解析命令格式: gpio <pin> <level>
    char* token = strtok(cmd, " ");
    if (token == NULL || strcmp(token, "gpio") != 0) {
        ESP_LOGI(TAG, "错误: 命令必须以 'gpio' 开头");
        return;
    }

    // 获取引脚号
    token = strtok(NULL, " ");
    if (token == NULL) {
        ESP_LOGI(TAG, "错误: 缺少引脚号");
        ESP_LOGI(TAG, "用法: gpio <pin> <1/0>");
        return;
    }

    int pin_num = atoi(token);
    
    // 验证引脚号范围 (ESP32-C6常用GPIO范围)
    if (pin_num < 0 || pin_num > 30) {
        ESP_LOGI(TAG, "错误: 引脚号 %d 超出范围 (0-30)", pin_num);
        return;
    }

    // 获取电平值
    token = strtok(NULL, " ");
    if (token == NULL) {
        ESP_LOGI(TAG, "错误: 缺少电平值");
        ESP_LOGI(TAG, "用法: gpio <pin> <1/0>");
        return;
    }

    int level;
    if (strcmp(token, "1") == 0) {
        level = 1;
    } else if (strcmp(token, "0") == 0) {
        level = 0;
    } else {
        ESP_LOGI(TAG, "错误: 电平值必须是 1 或 0，收到: %s", token);
        return;
    }

    // 执行GPIO控制
    gpio_num_t gpio_pin = pin_num;

    // 重置引脚以确保其为GPIO功能
    gpio_reset_pin(gpio_pin);

    // 配置为输出
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d 配置失败: %s", pin_num, esp_err_to_name(ret));
        return;
    }

    // 设置电平
    gpio_set_level(gpio_pin, level);
    
    ESP_LOGI(TAG, "成功: GPIO%d 设置为 %s (%.1fV)", 
             pin_num, 
             level ? "高电平" : "低电平",
             level ? 3.3 : 0.0);
}