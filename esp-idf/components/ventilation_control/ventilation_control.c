#include "ventilation_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_control.h"

static const char *TAG = "VENT_CTRL";

// 默认通风周期（55分钟通风5分钟）-> 用于测试：20秒通风10秒
 #define DEFAULT_VENT_ON_SECONDS  300   // 通风5分钟 300 
 #define DEFAULT_VENT_OFF_SECONDS 3300  // 关闭55分钟 3300
// 测试参数：
// #define DEFAULT_VENT_ON_SECONDS  10   // 通风10秒
// #define DEFAULT_VENT_OFF_SECONDS 20   // 关闭20秒

// 模块内部全局变量
static int s_gpio_pin = 1;  // 默认GPIO1
static int s_vent_on_seconds = DEFAULT_VENT_ON_SECONDS;
static int s_vent_off_seconds = DEFAULT_VENT_OFF_SECONDS;
static TaskHandle_t s_ventilation_task_handle = NULL;
static bool s_task_running = false;
static bool s_current_state = false;

// 通风控制任务函数
static void ventilation_task(void *arg) {
    ESP_LOGI(TAG, "Ventilation control task started, GPIO pin: %d", s_gpio_pin);
    ESP_LOGI(TAG, "Ventilation cycle: ON %d seconds, OFF %d seconds", 
             s_vent_on_seconds, s_vent_off_seconds);

    while (1) {
        // 通风阶段：关闭
        ESP_LOGI(TAG, "Ventilation OFF (GPIO%d -> LOW)", s_gpio_pin);
        gpio_control_set_level(s_gpio_pin, false);
        vTaskDelay(s_vent_off_seconds * 1000 / portTICK_PERIOD_MS);

        // 通风阶段：开启
        ESP_LOGI(TAG, "Ventilation ON (GPIO%d -> HIGH)", s_gpio_pin);
        gpio_control_set_level(s_gpio_pin, true);
        vTaskDelay(s_vent_on_seconds * 1000 / portTICK_PERIOD_MS);
    }
}

// 初始化通风控制模块
void ventilation_control_init(int gpio_pin) {
    s_gpio_pin = gpio_pin;
    ESP_LOGI(TAG, "Ventilation control module initialized, control pin: GPIO%d", s_gpio_pin);
}

// 获取通风控制当前状态
bool ventilation_control_get_state(void) {
    return s_current_state;
}

// 获取通风控制引脚号
int ventilation_control_get_pin(void) {
    return s_gpio_pin;
}

// 启动通风控制任务
void ventilation_control_start(void) {
    if (s_task_running) {
        ESP_LOGW(TAG, "Ventilation control task is already running");
        return;
    }

    ESP_LOGI(TAG, "Starting ventilation control task...");
    xTaskCreate(ventilation_task,        // 任务函数
                "ventilation_task",      // 任务名称
                4096,                    // 堆栈大小
                NULL,                    // 任务参数
                5,                       // 任务优先级
                &s_ventilation_task_handle); // 任务句柄
    
    if (s_ventilation_task_handle != NULL) {
        s_task_running = true;
        ESP_LOGI(TAG, "Ventilation control task created successfully");
    } else {
        ESP_LOGE(TAG, "Ventilation control task creation failed");
    }
}

// 停止通风控制任务
void ventilation_control_stop(void) {
    if (!s_task_running || s_ventilation_task_handle == NULL) {
        ESP_LOGW(TAG, "Ventilation control task is not running");
        return;
    }

    ESP_LOGI(TAG, "Stopping ventilation control task...");
    vTaskDelete(s_ventilation_task_handle);
    s_ventilation_task_handle = NULL;
    s_task_running = false;
    
    // 确保GPIO输出为低电平（关闭通风）
    gpio_control_set_level(s_gpio_pin, false);
    ESP_LOGI(TAG, "Ventilation control task has been stopped, GPIO%d is set to LOW", s_gpio_pin);
}

// 设置通风周期参数（用于测试，可以动态调整）
void ventilation_control_set_timing(int vent_on_seconds, int vent_off_seconds) {
    if (vent_on_seconds <= 0 || vent_off_seconds <= 0) {
        ESP_LOGE(TAG, "Error: ventilation timing parameters must be positive");
        return;
    }

    s_vent_on_seconds = vent_on_seconds;
    s_vent_off_seconds = vent_off_seconds;
    ESP_LOGI(TAG, "Ventilation timing parameters updated: ON %d seconds, OFF %d seconds", 
             s_vent_on_seconds, s_vent_off_seconds);
}