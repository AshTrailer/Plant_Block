#include "pwm_test.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "PWM_TEST";

// PWM配置
#define PWM_TIMER          LEDC_TIMER_0
#define PWM_MODE           LEDC_LOW_SPEED_MODE
#define PWM_CHANNEL        LEDC_CHANNEL_0
#define PWM_FREQUENCY_HZ   19000     // 20kHz
#define PWM_RESOLUTION     LEDC_TIMER_12_BIT
#define PWM_MAX_DUTY       4095  // 2^12 - 1

static int s_pwm_pin = -1;
static bool s_pwm_initialized = false;
static uint8_t s_current_duty = 0;

// 检查最小导通时间限制（0.7ms对应占空比）
static bool check_minimum_duty(uint8_t duty_percent) {
    // 20kHz周期 = 50μs
    // 最小导通时间0.7ms = 700μs
    // 最小占空比 = 700 / 50000 = 1.4%
    if (duty_percent < 1.4 && duty_percent > 0) {
        ESP_LOGE(TAG, "占空比%.1f%%低于最小导通时间限制(1.4%%)", (float)duty_percent);
        return false;
    }
    return true;
}

// 初始化PWM测试模块
void pwm_test_init(int gpio_pin) {
    if (s_pwm_initialized) {
        ESP_LOGW(TAG, "PWM已经初始化");
        return;
    }
    
    s_pwm_pin = gpio_pin;
    
    // 配置定时器
    ledc_timer_config_t timer_config = {
        .speed_mode       = PWM_MODE,
        .timer_num        = PWM_TIMER,
        .duty_resolution  = PWM_RESOLUTION,
        .freq_hz          = PWM_FREQUENCY_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PWM定时器配置失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 配置通道
    ledc_channel_config_t channel_config = {
        .gpio_num       = s_pwm_pin,
        .speed_mode     = PWM_MODE,
        .channel        = PWM_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = PWM_TIMER,
        .duty           = 0,  // 初始占空比为0
        .hpoint         = 0
    };
    
    ret = ledc_channel_config(&channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PWM通道配置失败: %s", esp_err_to_name(ret));
        return;
    }
    
    s_pwm_initialized = true;
    s_current_duty = 0;
    
    ESP_LOGI(TAG, "PWM测试模块初始化完成");
    ESP_LOGI(TAG, "引脚: GPIO%d", s_pwm_pin);
    ESP_LOGI(TAG, "频率: %d Hz", PWM_FREQUENCY_HZ);
    ESP_LOGI(TAG, "分辨率: 11位 (0-%d)", PWM_MAX_DUTY);
    ESP_LOGI(TAG, "最小导通时间: 0.7ms (最小占空比: 1.4%%)");
}

// 设置PWM占空比 (0-100%)
bool pwm_test_set_duty(uint8_t duty_percent) {
    if (!s_pwm_initialized) {
        ESP_LOGE(TAG, "PWM未初始化");
        return false;
    }
    
    if (duty_percent > 100) {
        ESP_LOGE(TAG, "占空比超出范围: %d (0-100)", duty_percent);
        return false;
    }
    
    // 检查最小导通时间限制（除了0%关闭）
    if (duty_percent > 0 && !check_minimum_duty(duty_percent)) {
        return false;
    }
    
    // 计算实际占空比值
    uint32_t duty_value = 0;
    if (duty_percent > 0) {
        duty_value = (uint32_t)((float)duty_percent / 100.0f * (float)PWM_MAX_DUTY);
        
        // 确保最小占空比值（对应0.7ms）
        uint32_t min_duty_value = (uint32_t)(1.4f / 100.0f * (float)PWM_MAX_DUTY);
        if (duty_value < min_duty_value) {
            duty_value = min_duty_value;
        }
    }
    
    // 更新占空比
    esp_err_t ret = ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置占空比失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 更新输出
    ret = ledc_update_duty(PWM_MODE, PWM_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "更新占空比输出失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    s_current_duty = duty_percent;
    
    ESP_LOGI(TAG, "PWM占空比设置为: %d%% (值: %d)", duty_percent, duty_value);
    return true;
}

// 停止PWM输出
void pwm_test_stop(void) {
    if (!s_pwm_initialized) {
        return;
    }
    
    ledc_stop(PWM_MODE, PWM_CHANNEL, 0);
    s_current_duty = 0;
    ESP_LOGI(TAG, "PWM输出已停止");
}

// 处理PWM测试命令
void pwm_test_process_command(const char* command) {
    if (command == NULL) return;
    
    ESP_LOGI(TAG, "处理PWM命令: %s", command);
    
    if (strncmp(command, "pwm", 3) == 0) {
        // 格式: pwm <duty_percent>
        int duty_percent;
        
        if (sscanf(command, "pwm %d", &duty_percent) == 1) {
            if (pwm_test_set_duty(duty_percent)) {
                ESP_LOGI(TAG, "PWM占空比设置成功: %d%%", duty_percent);
            } else {
                ESP_LOGI(TAG, "PWM占空比设置失败");
            }
        } else if (strcmp(command, "pwm stop") == 0) {
            pwm_test_stop();
            ESP_LOGI(TAG, "PWM输出已停止");
        } else if (strcmp(command, "pwm status") == 0) {
            ESP_LOGI(TAG, "=== PWM状态 ===");
            ESP_LOGI(TAG, "初始化: %s", s_pwm_initialized ? "是" : "否");
            ESP_LOGI(TAG, "当前占空比: %d%%", s_current_duty);
            ESP_LOGI(TAG, "控制引脚: GPIO%d", s_pwm_pin);
            ESP_LOGI(TAG, "频率: %d Hz", PWM_FREQUENCY_HZ);
            ESP_LOGI(TAG, "==============");
        } else if (strcmp(command, "pwm help") == 0) {
            ESP_LOGI(TAG, "=== PWM测试命令帮助 ===");
            ESP_LOGI(TAG, "pwm <0-100>      - 设置PWM占空比 (0-100%%)");
            ESP_LOGI(TAG, "pwm stop         - 停止PWM输出");
            ESP_LOGI(TAG, "pwm status       - 显示PWM状态");
            ESP_LOGI(TAG, "pwm help         - 显示此帮助");
            ESP_LOGI(TAG, "注意: 最小导通时间0.7ms (最小占空比1.4%%)");
            ESP_LOGI(TAG, "示例: pwm 50, pwm 0, pwm 100");
            ESP_LOGI(TAG, "====================");
        } else {
            ESP_LOGI(TAG, "格式错误，正确格式: pwm <0-100>");
            ESP_LOGI(TAG, "示例: pwm 50");
            ESP_LOGI(TAG, "输入 'pwm help' 查看帮助");
        }
    } else {
        ESP_LOGI(TAG, "未知命令，输入 'pwm help' 查看帮助");
    }
}