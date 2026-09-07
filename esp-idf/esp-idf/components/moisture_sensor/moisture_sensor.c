#include "moisture_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "gpio_control.h"
#include "data_processor.h"
#include "cloud_comm.h"
#include <string.h>
#include <math.h>

static const char *TAG = "MOISTURE_SENSOR";

#define MOISTURE_LOGI(fmt, ...) do { \
    ESP_LOGI(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[I] " fmt, ##__VA_ARGS__); \
} while(0)

#define MOISTURE_LOGE(fmt, ...) do { \
    ESP_LOGE(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[E] " fmt, ##__VA_ARGS__); \
} while(0)

#define MOISTURE_LOGW(fmt, ...) do { \
    ESP_LOGW(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[W] " fmt, ##__VA_ARGS__); \
} while(0)

// 硬件配置
static int s_power_pin = 15;
static int s_adc_pin = 2;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static TaskHandle_t s_read_task_handle = NULL;
static bool s_is_powered = false;

// 校准数据
static bool s_dry_calibrated = false;
static bool s_wet_calibrated = false;
static float s_raw_dry = 0;      // 干燥时原始ADC平均值
static float s_raw_wet = 0;      // 湿润时原始ADC平均值
static float s_volt_efuse_dry = 0; // 干燥时eFuse校准电压平均值
static float s_volt_efuse_wet = 0; // 湿润时eFuse校准电压平均值

// 目标电压（根据示波器实测）
#define TARGET_VOLT_DRY 2079.0f   // 2.079V = 2079 mV
#define TARGET_VOLT_WET 970.0f    // 970 mV

// 线性拟合系数（用于将eFuse电压映射到目标电压）
static float s_k = 1.0f;
static float s_b = 0.0f;

// 稳定性阈值
#define DRY_STABILITY_THRESHOLD 0.5f   // 干燥校准阈值 0.5%
#define WET_STABILITY_THRESHOLD 1.0f   // 湿润校准阈值 1.0% (放宽)

// 采样配置
#define SAMPLE_COUNT 10
#define MAX_RETRY 3

// ADC 校准初始化（eFuse）
static bool adc_calibration_init(void) {
    esp_err_t ret;
    adc_cali_handle_t cali_handle = NULL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_2,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);
    if (ret == ESP_OK) calibrated = true;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle);
    if (ret == ESP_OK) calibrated = true;
#endif

    s_adc_cali_handle = cali_handle;
    if (!calibrated) {
        MOISTURE_LOGW("eFuse calibration failed - fallback to raw * 3300 / 4095");
    }
    return calibrated;
}

// ADC 硬件初始化
static void adc_init(void) {
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,//ADC_UNIT_1
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_4, &chan_cfg));//ADC_CHANNEL_2
    MOISTURE_LOGI("ADC configured: GPIO%d (ADC1_CH2), 12-bit, 0-3.3V", s_adc_pin);

    adc_calibration_init();
}

// 读取原始ADC值
static uint32_t adc_read_raw(void) {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, ADC_CHANNEL_4, &raw));//ADC_CHANNEL_2
    return (uint32_t)raw;
}

// 读取eFuse校准后的电压（mV）
static uint32_t adc_read_voltage_efuse(void) {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, ADC_CHANNEL_4, &raw));//ADC_CHANNEL_2
    int voltage_mv = 0;
    if (s_adc_cali_handle != NULL) {
        esp_err_t ret = adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &voltage_mv);
        if (ret != ESP_OK) {
            voltage_mv = (raw * 3300) / 4095;
        }
    } else {
        voltage_mv = (raw * 3300) / 4095;
    }
    return (uint32_t)voltage_mv;
}

// 应用二次校准：将eFuse电压映射到目标电压
static uint32_t apply_secondary_calibration(uint32_t volt_efuse) {
    if (!s_dry_calibrated || !s_wet_calibrated) {
        return volt_efuse; // 未完成双校准时返回原始eFuse电压
    }
    // 线性映射
    float calibrated = s_k * volt_efuse + s_b;
    return (uint32_t)calibrated;
}

// 计算湿度百分比（干燥=100%，湿润=0%）
static float calculate_humidity_percent(uint32_t volt_calibrated) {
    float dry_target = TARGET_VOLT_DRY;
    float wet_target = TARGET_VOLT_WET;
    float range = dry_target - wet_target;
    if (range <= 0) return 0.0f;
    float hum = (dry_target - volt_calibrated) / range * 100.0f;
    if (hum < 0) hum = 0;
    if (hum > 100) hum = 100;
    return hum;
}

// 连续采集任务
static void sensor_read_task(void *arg) {
    MOISTURE_LOGI("Moisture sensor reading task started, waiting 2s for stabilization...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    while (1) {
        if (!s_is_powered) {
            MOISTURE_LOGI("Moisture sensor power off, stop reading");
            break;
        }
        uint32_t volt_efuse = adc_read_voltage_efuse();
        uint32_t volt_cal = apply_secondary_calibration(volt_efuse);
        float hum = calculate_humidity_percent(volt_cal);

        MOISTURE_LOGI("Moisture sensor: Voltage=%lu mV, Humidity=%.1f%%", volt_cal, hum);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    s_read_task_handle = NULL;
    vTaskDelete(NULL);
}

// 初始化
void moisture_sensor_init(int power_pin, int adc_pin) {
    s_power_pin = power_pin;
    s_adc_pin = adc_pin;
    adc_init();
    MOISTURE_LOGI("Moisture sensor module initialized");
    MOISTURE_LOGI("Power pin: GPIO%d, ADC pin: GPIO%d", s_power_pin, s_adc_pin);
}

void moisture_sensor_power_on(void) {
    if (s_is_powered) {
        MOISTURE_LOGW("Moisture sensor already powered on");
        return;
    }
    gpio_control_set_level(s_power_pin, true);
    s_is_powered = true;
    MOISTURE_LOGI("Moisture sensor power ON");
}

void moisture_sensor_power_off(void) {
    if (!s_is_powered) {
        MOISTURE_LOGW("Moisture sensor already powered off");
        return;
    }
    gpio_control_set_level(s_power_pin, false);
    s_is_powered = false;
    MOISTURE_LOGI("Moisture sensor power OFF");
    moisture_sensor_stop_reading();
}

bool moisture_sensor_is_powered(void) {
    return s_is_powered;
}

void moisture_sensor_start_reading(void) {
    if (!s_is_powered) {
        MOISTURE_LOGE("Cannot start reading: sensor power is off");
        return;
    }
    if (s_read_task_handle != NULL) {
        MOISTURE_LOGW("Reading task already running");
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
    MOISTURE_LOGI("Started continuous moisture reading (1s interval)");
}

void moisture_sensor_stop_reading(void) {
    if (s_read_task_handle != NULL) {
        vTaskDelete(s_read_task_handle);
        s_read_task_handle = NULL;
        MOISTURE_LOGI("Stopped continuous moisture reading");
    }
}

// ---------- 校准辅助函数 ----------

/**
 * @brief 采集一组样本并进行稳定性处理，带重试机制
 * @param raw_avg 输出：原始ADC平均值
 * @param volt_avg 输出：eFuse电压平均值
 * @param max_retry 最大重试次数
 * @return true 成功，false 失败
 */
static bool collect_stable_samples_with_retry(float *raw_avg, float *volt_avg, int max_retry, float threshold) {
    int raw_buffer[SAMPLE_COUNT];
    int volt_buffer[SAMPLE_COUNT];

    for (int attempt = 1; attempt <= max_retry; attempt++) {
        MOISTURE_LOGI("Attempt %d/%d: collecting %d samples (1s interval)...", attempt, max_retry, SAMPLE_COUNT);

        for (int i = 0; i < SAMPLE_COUNT; i++) {
            raw_buffer[i] = (int)adc_read_raw();
            volt_buffer[i] = (int)adc_read_voltage_efuse();
            ESP_LOGI(TAG, "Sample %2d: raw=%4d, volt_efuse=%4d mV", i+1, raw_buffer[i], volt_buffer[i]);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }

        // 对 raw 数据去除最大最小值
        int raw_no_outliers[SAMPLE_COUNT];
        int raw_count = data_processor_remove_outliers(raw_buffer, SAMPLE_COUNT, raw_no_outliers, 3);
        if (raw_count < 3) {
            MOISTURE_LOGW("Attempt %d: raw data insufficient after outlier removal", attempt);
            goto retry_delay;
        }

        float raw_mean = data_processor_mean(raw_no_outliers, raw_count);
        float raw_stddev = data_processor_stddev(raw_no_outliers, raw_count);
        float raw_stddev_percent = (raw_stddev / raw_mean) * 100.0f;
        MOISTURE_LOGI("Raw: mean=%.2f, stddev=%.2f (%.2f%%)", raw_mean, raw_stddev, raw_stddev_percent);

        if (raw_stddev_percent > threshold) {
            MOISTURE_LOGW("Attempt %d: raw stddev %.2f%% > threshold %.2f%%", attempt, raw_stddev_percent, threshold);
            goto retry_delay;
        }

        // 对电压数据去除最大最小值
        int volt_no_outliers[SAMPLE_COUNT];
        int volt_count = data_processor_remove_outliers(volt_buffer, SAMPLE_COUNT, volt_no_outliers, 3);
        if (volt_count < 3) {
            MOISTURE_LOGW("Attempt %d: voltage data insufficient after outlier removal", attempt);
            goto retry_delay;
        }

        float volt_mean = data_processor_mean(volt_no_outliers, volt_count);
        float volt_stddev = data_processor_stddev(volt_no_outliers, volt_count);
        float volt_stddev_percent = (volt_stddev / volt_mean) * 100.0f;
        MOISTURE_LOGI("Voltage: mean=%.2f mV, stddev=%.2f (%.2f%%)", volt_mean, volt_stddev, volt_stddev_percent);

        if (volt_stddev_percent > threshold) {
            MOISTURE_LOGW("Attempt %d: voltage stddev %.2f%% > threshold %.2f%%", attempt, volt_stddev_percent, threshold);
            goto retry_delay;
        }

        // 成功
        *raw_avg = raw_mean;
        *volt_avg = volt_mean;
        return true;

    retry_delay:
        if (attempt < max_retry) {
            MOISTURE_LOGI("Retrying in 2 seconds...");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }

    MOISTURE_LOGE("All %d attempts failed", max_retry);
    return false;
}

// 干燥校准
bool moisture_sensor_cal_dry(void) {
    bool was_powered = s_is_powered;   // 记录校准前的电源状态

    // 如果电源未开启，自动上电并等待2秒稳定
    if (!was_powered) {
        moisture_sensor_power_on();
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    // 停止任何正在运行的连续采集任务，避免干扰
    moisture_sensor_stop_reading();

    MOISTURE_LOGI("=== Dry Calibration ===");
    MOISTURE_LOGI("Please ensure sensor is in dry air and stable.");
    MOISTURE_LOGI("Sampling will take about %d seconds.", SAMPLE_COUNT);

    float raw_mean, volt_mean;
    if (!collect_stable_samples_with_retry(&raw_mean, &volt_mean, MAX_RETRY, DRY_STABILITY_THRESHOLD)) {
        MOISTURE_LOGE("Dry calibration failed: data unstable after retries.");
        // 校准失败，若电源原本是关的，则恢复关断
        if (!was_powered) {
            moisture_sensor_power_off();
        }
        return false;
    }

    s_raw_dry = raw_mean;
    s_volt_efuse_dry = volt_mean;
    s_dry_calibrated = true;
    MOISTURE_LOGI("Dry calibration successful:");
    MOISTURE_LOGI("  Raw avg: %.2f, eFuse voltage avg: %.2f mV", raw_mean, volt_mean);
    MOISTURE_LOGI("  Target voltage (scope): %.0f mV", TARGET_VOLT_DRY);

    if (s_wet_calibrated) {
        // 双校准完成，计算线性拟合系数
        s_k = (TARGET_VOLT_DRY - TARGET_VOLT_WET) / (s_volt_efuse_dry - s_volt_efuse_wet);
        s_b = TARGET_VOLT_DRY - s_k * s_volt_efuse_dry;
        MOISTURE_LOGI("Linear calibration computed: k=%.6f, b=%.2f", s_k, s_b);
        MOISTURE_LOGI("Now continuous readings will show calibrated voltage and humidity.");
    } else {
        MOISTURE_LOGI("Wet calibration still needed to complete dual-point calibration.");
    }

    // 若电源原本是关的，校准完成后恢复关断
    if (!was_powered) {
        moisture_sensor_power_off();
    }
    return true;
}

// 湿润校准
bool moisture_sensor_cal_wet(void) {
    bool was_powered = s_is_powered;

    if (!was_powered) {
        moisture_sensor_power_on();
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    moisture_sensor_stop_reading();

    MOISTURE_LOGI("=== Wet Calibration ===");
    MOISTURE_LOGI("Please ensure sensor is in wet environment and stable.");
    MOISTURE_LOGI("Sampling will take about %d seconds.", SAMPLE_COUNT);

    float raw_mean, volt_mean;
    if (!collect_stable_samples_with_retry(&raw_mean, &volt_mean, MAX_RETRY, WET_STABILITY_THRESHOLD)) {
        MOISTURE_LOGE("Wet calibration failed: data unstable after retries.");
        if (!was_powered) {
            moisture_sensor_power_off();
        }
        return false;
    }

    s_raw_wet = raw_mean;
    s_volt_efuse_wet = volt_mean;
    s_wet_calibrated = true;
    MOISTURE_LOGI("Wet calibration successful:");
    MOISTURE_LOGI("  Raw avg: %.2f, eFuse voltage avg: %.2f mV", raw_mean, volt_mean);
    MOISTURE_LOGI("  Target voltage (scope): %.0f mV", TARGET_VOLT_WET);

    if (s_dry_calibrated) {
        s_k = (TARGET_VOLT_DRY - TARGET_VOLT_WET) / (s_volt_efuse_dry - s_volt_efuse_wet);
        s_b = TARGET_VOLT_DRY - s_k * s_volt_efuse_dry;
        MOISTURE_LOGI("Linear calibration computed: k=%.6f, b=%.2f", s_k, s_b);
        MOISTURE_LOGI("Now continuous readings will show calibrated voltage and humidity.");
    } else {
        MOISTURE_LOGI("Dry calibration still needed to complete dual-point calibration.");
    }

    if (!was_powered) {
        moisture_sensor_power_off();
    }
    return true;
}

bool moisture_sensor_is_dry_calibrated(void) {
    return s_dry_calibrated;
}

bool moisture_sensor_is_wet_calibrated(void) {
    return s_wet_calibrated;
}

bool moisture_sensor_get_calibration(float *k, float *b) {
    if (!s_dry_calibrated || !s_wet_calibrated) return false;
    *k = s_k;
    *b = s_b;
    return true;
}

// 获取当前校准后的电压（用于连续采集）
uint32_t moisture_sensor_get_calibrated_voltage(void) {
    uint32_t volt_efuse = adc_read_voltage_efuse();
    return apply_secondary_calibration(volt_efuse);
}

// 获取当前湿度百分比
float moisture_sensor_get_humidity_percent(void) {
    uint32_t volt_cal = moisture_sensor_get_calibrated_voltage(); // 未校准时返回原始eFuse电压
    return calculate_humidity_percent(volt_cal);
}