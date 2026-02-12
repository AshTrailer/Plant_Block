#include "moisture_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "gpio_control.h"
#include <string.h>

static const char *TAG = "MOISTURE_SENSOR";

// 硬件配置
static int s_power_pin = 15;        // 默认GPIO15
static int s_adc_pin = 2;          // 默认GPIO2
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static TaskHandle_t s_read_task_handle = NULL;
static bool s_is_powered = false;

// ADC 初始化
static void adc_init(void) {
    // 配置ADC单次转换单元
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,      // 使用ADC1
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));

    // 配置ADC通道（GPIO18对应ADC1_CH0？实际请查阅数据手册）
    // 此处假定GPIO18为ADC1_CH0，用户需根据实际硬件修改
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,   // 0~3.3V
        .bitwidth = ADC_BITWIDTH_12, // 12位分辨率
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_2, &chan_cfg));
    ESP_LOGI(TAG, "ADC initialized on GPIO%d", s_adc_pin);
}

// 读取ADC原始值（0-4095）
static uint32_t adc_read_raw(void) {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, ADC_CHANNEL_2, &raw));
    return (uint32_t)raw;
}

// 将原始值转换为电压（mV）
static uint32_t adc_raw_to_mv(uint32_t raw) {
    // 12位ADC，参考电压3.3V
    return (raw * 3300) / 4095;
}

// 连续采集任务
static void sensor_read_task(void *arg) {
    ESP_LOGI(TAG, "Sensor reading task started, waiting 2s for sensor stabilization...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);  // 上电后稳定2秒

    while (1) {
        if (!s_is_powered) {
            ESP_LOGI(TAG, "Sensor power off, stop reading");
            break;
        }

        // 读取ADC并打印电压
        uint32_t raw = adc_read_raw();
        uint32_t mv = adc_raw_to_mv(raw);
        ESP_LOGI(TAG, "Moisture sensor raw: %u, voltage: %u mV", raw, mv);

        vTaskDelay(1000 / portTICK_PERIOD_MS); // 每秒采集一次
    }

    s_read_task_handle = NULL;
    vTaskDelete(NULL);
}

// 初始化模块
void moisture_sensor_init(int power_pin, int adc_pin) {
    s_power_pin = power_pin;
    s_adc_pin = adc_pin;

    // ADC初始化
    adc_init();

    ESP_LOGI(TAG, "Moisture sensor module initialized");
    ESP_LOGI(TAG, "Power pin: GPIO%d, ADC pin: GPIO%d", s_power_pin, s_adc_pin);
}

// 打开传感器电源
void moisture_sensor_power_on(void) {
    if (s_is_powered) {
        ESP_LOGW(TAG, "Sensor already powered on");
        return;
    }
    gpio_control_set_level(s_power_pin, true);
    s_is_powered = true;
    ESP_LOGI(TAG, "Sensor power ON");
}

// 关闭传感器电源
void moisture_sensor_power_off(void) {
    if (!s_is_powered) {
        ESP_LOGW(TAG, "Sensor already powered off");
        return;
    }
    gpio_control_set_level(s_power_pin, false);
    s_is_powered = false;
    ESP_LOGI(TAG, "Sensor power OFF");

    // 停止正在运行的采集任务
    moisture_sensor_stop_reading();
}

// 获取电源状态
bool moisture_sensor_is_powered(void) {
    return s_is_powered;
}

// 启动连续采集
void moisture_sensor_start_reading(void) {
    if (!s_is_powered) {
        ESP_LOGE(TAG, "Cannot start reading: sensor power is off");
        return;
    }

    if (s_read_task_handle != NULL) {
        ESP_LOGW(TAG, "Reading task already running");
        return;
    }

    xTaskCreate(
        sensor_read_task,
        "sensor_read_task",
        3072,
        NULL,
        2,
        &s_read_task_handle
    );
    ESP_LOGI(TAG, "Started continuous moisture reading (1s interval)");
}

// 手动停止采集
void moisture_sensor_stop_reading(void) {
    if (s_read_task_handle != NULL) {
        vTaskDelete(s_read_task_handle);
        s_read_task_handle = NULL;
        ESP_LOGI(TAG, "Stopped continuous moisture reading");
    }
}