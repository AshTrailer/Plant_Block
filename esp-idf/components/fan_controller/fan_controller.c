#include "fan_controller.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "FAN_CONTROLLER";

// 风扇控制结构体
typedef struct {
    gpio_num_t pin;
    bool is_on;
    bool initialized;
} fan_state_t;

static fan_state_t fan_state = {
    .pin = FAN_GPIO_PIN,
    .is_on = false,
    .initialized = false
};

bool fan_controller_init(void) {
    if (fan_state.initialized) {
        ESP_LOGI(TAG, "Already initialized");
        return true;
    }

    // 配置GPIO为输出模式
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << fan_state.pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // 外部已有下拉电阻
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO配置失败: %s", esp_err_to_name(ret));
        return false;
    }

    // 初始状态：关闭风扇
    gpio_set_level(fan_state.pin, 0);
    fan_state.is_on = false;
    fan_state.initialized = true;
    
    ESP_LOGI(TAG, "风扇控制器初始化完成，GPIO %d", fan_state.pin);
    return true;
}

bool fan_controller_set_state(bool state) {
    if (!fan_state.initialized) {
        ESP_LOGE(TAG, "未初始化");
        return false;
    }

    // 设置GPIO电平
    // 高电平（1）-> NMOSFET导通 -> 风扇启动
    // 低电平（0）-> NMOSFET关断 -> 风扇停止
    gpio_set_level(fan_state.pin, state ? 1 : 0);
    fan_state.is_on = state;
    
    ESP_LOGI(TAG, "风扇 %s", state ? "启动" : "停止");
    return true;
}

bool fan_controller_get_state(void) {
    return fan_state.is_on;
}

bool fan_controller_toggle(void) {
    if (!fan_state.initialized) {
        return false;
    }
    bool new_state = !fan_state.is_on;
    return fan_controller_set_state(new_state);
}

void fan_controller_deinit(void) {
    if (fan_state.initialized) {
        // 先关闭风扇
        gpio_set_level(fan_state.pin, 0);
        // 重置GPIO
        gpio_reset_pin(fan_state.pin);
        
        fan_state.is_on = false;
        fan_state.initialized = false;
        ESP_LOGI(TAG, "风扇控制器反初始化完成");
    }
}