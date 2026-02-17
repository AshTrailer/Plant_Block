#include "gpio_control.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include "cloud_comm.h"

static const char *TAG = "GPIO_CONTROL";

#define GPIO_LOGE(fmt, ...) do { \
    ESP_LOGE(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[E] " fmt, ##__VA_ARGS__); \
} while(0)

#define GPIO_LOGW(fmt, ...) do { \
    ESP_LOGW(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[W] " fmt, ##__VA_ARGS__); \
} while(0)

#define GPIO_LOGI(fmt, ...) do { \
    ESP_LOGI(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[I] " fmt, ##__VA_ARGS__); \
} while(0)

// 初始化时配置的GPIO引脚列表
static const int s_managed_pins[] = {1, 3, 4, 5, 6, 7, 8, 9, 14, 15, 18};
//static const int s_managed_pins[] = {14,25,2};
static const int s_managed_pin_count = sizeof(s_managed_pins) / sizeof(s_managed_pins[0]);

// 检查引脚号是否在受管理的列表中
static bool is_pin_managed(int pin) {
    for (int i = 0; i < s_managed_pin_count; i++) {
        if (s_managed_pins[i] == pin) {
            return true;
        }
    }
    return false;
}

// 初始化模块：将预设的多个GPIO引脚配置为低电平输出
void gpio_control_init(void) {
    esp_err_t ret;
    
    GPIO_LOGI("Starting configuration of %d GPIO pins to low level...", s_managed_pin_count);
    
    for (int i = 0; i < s_managed_pin_count; i++) {
        gpio_num_t pin_num = s_managed_pins[i];
        
        // 重置引脚，解除所有可能存在的功能复用
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
            GPIO_LOGE("GPIO Pin %d config failed: %s", pin_num, esp_err_to_name(ret));
            continue;
        }
        
        // 初始设置为低电平
        gpio_set_level(pin_num, 0);
        
        GPIO_LOGI("GPIO Pin %d set to low level", pin_num);
    }
    
    GPIO_LOGI("GPIO Control Module Initialization Complete");
    GPIO_LOGI("Available Pins: 1, 3, 4, 5, 6, 7, 8, 9, 14, 15, 18");
}

// 控制指定GPIO引脚的电平
bool gpio_control_set_level(int pin, bool level) {
    // 验证引脚号是否在受管理列表中
    if (!is_pin_managed(pin)) {
        GPIO_LOGE("Error: Pin %d is not in managed list", pin);
        return false;
    }
    
    // 验证引脚号合法性
    if (pin < 0 || pin > GPIO_NUM_MAX) {
        GPIO_LOGE("Error: Pin %d is out of valid range", pin);
        return false;
    }
    
    // 执行GPIO电平设置
    gpio_num_t gpio_pin = pin;
    gpio_set_level(gpio_pin, level ? 1 : 0);
    
    GPIO_LOGI("GPIO Pin %d level set to: %s (%.1fV)", 
             pin, 
             level ? "High Level" : "Low Level",
             level ? 3.3 : 0.0);
    
    return true;
}

// 反初始化，重置GPIO（可选）
void gpio_control_deinit(void) {
    // 将所有测试引脚重置为默认状态
    gpio_reset_pin(GPIO_NUM_2);
    gpio_reset_pin(GPIO_NUM_3);
    gpio_reset_pin(GPIO_NUM_8);
    gpio_reset_pin(GPIO_NUM_9);
    
    GPIO_LOGI("GPIO Control Deinitialization Complete");
}   
