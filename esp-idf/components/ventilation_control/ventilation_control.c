#include "ventilation_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_control.h"

static const char *TAG = "VENT_CTRL";

// 默认通风周期（55分钟通风5分钟）-> 用于测试：20秒通风10秒
 #define DEFAULT_VENT_ON_SECONDS  5   // 通风5分钟
 #define DEFAULT_VENT_OFF_SECONDS 55  // 关闭55分钟
// 测试参数：
// #define DEFAULT_VENT_ON_SECONDS  10   // 通风10秒
// #define DEFAULT_VENT_OFF_SECONDS 20   // 关闭20秒

// 模块内部全局变量
static int s_gpio_pin = 15;  // 默认GPIO15
static int s_vent_on_seconds = DEFAULT_VENT_ON_SECONDS;
static int s_vent_off_seconds = DEFAULT_VENT_OFF_SECONDS;
static TaskHandle_t s_ventilation_task_handle = NULL;
static bool s_task_running = false;

// 通风控制任务函数
static void ventilation_task(void *arg) {
    ESP_LOGI(TAG, "通风控制任务开始，GPIO引脚: %d", s_gpio_pin);
    ESP_LOGI(TAG, "通风周期: 开启 %d 秒, 关闭 %d 秒", 
             s_vent_on_seconds, s_vent_off_seconds);

    while (1) {
        // 通风阶段：开启
        ESP_LOGI(TAG, "通风开启 (GPIO%d -> 高电平)", s_gpio_pin);
        gpio_control_set_level(s_gpio_pin, true);
        vTaskDelay(s_vent_on_seconds * 1000 / portTICK_PERIOD_MS);

        // 通风阶段：关闭
        ESP_LOGI(TAG, "通风关闭 (GPIO%d -> 低电平)", s_gpio_pin);
        gpio_control_set_level(s_gpio_pin, false);
        vTaskDelay(s_vent_off_seconds * 1000 / portTICK_PERIOD_MS);
    }
}

// 初始化通风控制模块
void ventilation_control_init(int gpio_pin) {
    s_gpio_pin = gpio_pin;
    ESP_LOGI(TAG, "通风控制模块初始化完成，控制引脚: GPIO%d", s_gpio_pin);
}

// 启动通风控制任务
void ventilation_control_start(void) {
    if (s_task_running) {
        ESP_LOGW(TAG, "通风控制任务已经在运行");
        return;
    }

    ESP_LOGI(TAG, "启动通风控制任务...");
    xTaskCreate(ventilation_task,        // 任务函数
                "ventilation_task",      // 任务名称
                4096,                    // 堆栈大小
                NULL,                    // 任务参数
                5,                       // 任务优先级
                &s_ventilation_task_handle); // 任务句柄
    
    if (s_ventilation_task_handle != NULL) {
        s_task_running = true;
        ESP_LOGI(TAG, "通风控制任务创建成功");
    } else {
        ESP_LOGE(TAG, "通风控制任务创建失败");
    }
}

// 停止通风控制任务
void ventilation_control_stop(void) {
    if (!s_task_running || s_ventilation_task_handle == NULL) {
        ESP_LOGW(TAG, "通风控制任务未运行");
        return;
    }

    ESP_LOGI(TAG, "停止通风控制任务...");
    vTaskDelete(s_ventilation_task_handle);
    s_ventilation_task_handle = NULL;
    s_task_running = false;
    
    // 确保GPIO输出为低电平（关闭通风）
    gpio_control_set_level(s_gpio_pin, false);
    ESP_LOGI(TAG, "通风控制任务已停止，GPIO%d 已设为低电平", s_gpio_pin);
}

// 设置通风周期参数（用于测试，可以动态调整）
void ventilation_control_set_timing(int vent_on_seconds, int vent_off_seconds) {
    if (vent_on_seconds <= 0 || vent_off_seconds <= 0) {
        ESP_LOGE(TAG, "错误: 通风时间参数必须为正数");
        return;
    }

    s_vent_on_seconds = vent_on_seconds;
    s_vent_off_seconds = vent_off_seconds;
    ESP_LOGI(TAG, "通风周期参数已更新: 开启 %d 秒, 关闭 %d 秒", 
             s_vent_on_seconds, s_vent_off_seconds);
}