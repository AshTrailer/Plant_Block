#include "irrigation_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time_manager.h"
#include "gpio_control.h"
#include "moisture_sensor.h"
#include "data_processor.h"
#include "cloud_comm.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "IRRIGATION";

#define IRR_LOGI(fmt, ...) do { \
    ESP_LOGI(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[I] " fmt, ##__VA_ARGS__); \
} while(0)

#define IRR_LOGE(fmt, ...) do { \
    ESP_LOGE(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[E] " fmt, ##__VA_ARGS__); \
} while(0)

#define IRR_LOGW(fmt, ...) do { \
    ESP_LOGW(TAG, fmt, ##__VA_ARGS__); \
    cloud_comm_publish_log("[W] " fmt, ##__VA_ARGS__); \
} while(0)

// 模块配置
typedef struct {
    int pump_pin;           // 水泵控制引脚（GPIO15）
    int trigger_threshold;  // 触发阈值（0-100%）
    float water_duration;   // 单次浇水时长（秒）
    int week_min_times;     // 周最小浇水次数
    int week_max_times;     // 周最大浇水次数
} irrigation_config_t;

// 模块状态
typedef struct {
    bool is_watering;       // 是否正在浇水
    bool sensor_power;      // 传感器电源状态
    int current_week;       // 当前周数（ISO8601周数）
    int week_water_count;   // 本周已浇水次数
    time_t last_water_time; // 上次浇水时间（Unix时间戳）
    time_t last_check_time; // 上次检查时间（Unix时间戳）
    bool test_mode;         // 测试模式（跳过4小时限制）
    bool time_reset_needed; // 时间重置标志
    bool force_check_needed;
} irrigation_state_t;

static irrigation_config_t s_config = {
    .pump_pin = 18,
    .trigger_threshold = 50,    // 默认阈值50%
    .water_duration = 31.5f,    // 默认31.5秒
    .week_min_times = 1,        // 默认周最小1次
    .week_max_times = 5,        // 默认周最大5次
};

static irrigation_state_t s_state = {
    .is_watering = false,
    .sensor_power = false,
    .current_week = 0,
    .week_water_count = 0,
    .last_water_time = 0,
    .last_check_time = 0,
    .test_mode = false,
    .time_reset_needed = false,
    .force_check_needed = false,
};

static TickType_t s_last_poll_ticks = 0;
static const TickType_t s_poll_interval_ticks = 1000 / portTICK_PERIOD_MS; // 1秒

#define SAMPLE_STABILITY_THRESHOLD 1.0f   // 1.0%

// 任务函数声明
static void watering_task(void *arg);
static void makeup_watering_task(void *arg);
static void sensor_check_task(void *arg);

// 检查是否为新的一周（周一00:00）
static bool is_new_week(void) {
    system_time_t current_time = time_manager_get_time();
    int current_weekday = time_manager_get_weekday();
    
    // 周一（weekday=1）且时间为00:00:00
    if (current_weekday == 1 && 
        current_time.hour == 0 && 
        current_time.minute == 0 && 
        current_time.second == 0) {
        
        // 调用time_manager的ISO周数计算
        int iso_week = time_manager_get_current_iso_week();
        
        // 如果周数不同，则是新的一周
        if (iso_week != s_state.current_week) {
            s_state.current_week = iso_week;
            return true;
        }
    }
    return false;
}

// 启动传感器电源（调用moisture_sensor模块）
static void start_sensor_power(void) {
    moisture_sensor_power_on();
    s_state.sensor_power = true;
}

// 关闭传感器电源（调用moisture_sensor模块）
static void stop_sensor_power(void) {
    moisture_sensor_power_off();
    s_state.sensor_power = false;
}

// 检查是否需要浇水（核心逻辑）
static bool should_water_now(int moisture_value) {
    // 1. 检查土壤湿度是否低于阈值
    if (moisture_value >= s_config.trigger_threshold) {
        IRR_LOGI("土壤湿度 %d%% 高于阈值 %d%%，不浇水", 
                moisture_value, s_config.trigger_threshold);
        return false;
    }
    
    // 2. 检查4小时内是否已经浇过水（测试模式下跳过）
    if (!s_state.test_mode) {
        if (s_state.last_water_time > 0) {
            time_t now = time_manager_get_unix_time();  // 使用time_manager的Unix时间戳
            double hours_since_last = difftime(now, s_state.last_water_time) / 3600.0;
            
            if (hours_since_last < 4.0) {
                IRR_LOGI("距离上次浇水 %.1f 小时，不足4小时，不浇水", hours_since_last);
                return false;
            }
        }
    }
    
    // 3. 检查本周浇水次数是否超过最大值
    if (s_state.week_water_count >= s_config.week_max_times) {
        IRR_LOGI("本周已浇水 %d 次，已达最大值 %d 次，不浇水",
                s_state.week_water_count, s_config.week_max_times);
        return false;
    }
    
    // 所有条件满足，可以浇水
    IRR_LOGI("触发浇水条件: 湿度 %d%% < 阈值 %d%%", 
            moisture_value, s_config.trigger_threshold);
    return true;
}

// 检查时间是否被重置（如果当前时间小于上次记录的时间，说明时间被设置到过去了）
static void check_time_reset(void) {
    if (s_state.time_reset_needed) {
        time_t current_time = time_manager_get_unix_time();

        // ===== 修复 1：重置周计数 =====
        s_state.current_week = time_manager_get_current_iso_week();
        s_state.week_water_count = 0;
        IRR_LOGI("时间重置：周数更新为 %d，本周浇水次数已清零", s_state.current_week);

        // ===== 修复 2：处理浇水时间（时间倒流时清零）=====
        if (s_state.last_water_time > 0 && difftime(s_state.last_water_time, current_time) > 0) {
            s_state.last_water_time = 0;   // 未来发生的浇水记录无效
            IRR_LOGI("时间重置：上次浇水时间在未来，已清零");
        }

        // ===== 修复 3：处理检查时间（时间倒流时重置）=====
        if (s_state.last_check_time > 0 && difftime(s_state.last_check_time, current_time) > 0) {
            s_state.last_check_time = current_time;
            IRR_LOGI("时间重置：上次检查时间已更新为当前时间");
        }

        // ===== 修复 4：设置强制检查标志 =====
        s_state.force_check_needed = true;

        s_state.time_reset_needed = false;
        IRR_LOGI("时间重置处理完成");
    }
}

// 采样并获取稳定的湿度值
static bool sample_stable_humidity(float *humidity_avg, int max_retry) {
    const int SAMPLE_COUNT = 5;
    int humidity_samples[SAMPLE_COUNT];
    int valid_samples[SAMPLE_COUNT];
    
    for (int attempt = 1; attempt <= max_retry; attempt++) {
        IRR_LOGI("湿度采样 尝试 %d/%d: 采集 %d 个样本 (间隔1秒)...", 
                 attempt, max_retry, SAMPLE_COUNT);
        
        // 采集样本
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            humidity_samples[i] = (int)moisture_sensor_get_humidity_percent();
            ESP_LOGI(TAG, "样本 %d: %d%%", i+1, humidity_samples[i]);
            if (i < SAMPLE_COUNT - 1) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
        
        // 去除最大最小值
        int valid_count = data_processor_remove_outliers(humidity_samples, SAMPLE_COUNT, 
                                                          valid_samples, 3);
        if (valid_count < 3) {
            IRR_LOGW("尝试 %d: 有效样本不足3个", attempt);
            goto retry_delay;
        }
        
        // 计算平均值和标准差
        float mean = data_processor_mean(valid_samples, valid_count);
        float stddev = data_processor_stddev(valid_samples, valid_count);
        
        // 处理均值为0的特殊情况
        if (mean < 0.01f) {  // 接近0
            if (stddev == 0) {
                // 所有值相等，视为稳定
                *humidity_avg = mean;
                IRR_LOGI("采样稳定（所有值相等），湿度=%.1f%%", mean);
                return true;
            } else {
                // 均值为0但存在波动，数据异常，不稳定
                IRR_LOGW("尝试 %d: 均值为0但标准差非零，数据异常", attempt);
                goto retry_delay;
            }
        }
        
        // 正常计算标准差百分比
        float stddev_percent = (stddev / mean) * 100.0f;
        IRR_LOGI("采样结果: 平均值=%.1f%%, 标准差=%.2f (%.2f%%)", mean, stddev, stddev_percent);
        
        if (stddev_percent <= SAMPLE_STABILITY_THRESHOLD) {
            *humidity_avg = mean;
            IRR_LOGI("采样稳定，湿度=%.1f%%", mean);
            return true;
        } else {
            IRR_LOGW("尝试 %d: 标准差 %.2f%% 超过阈值 %.2f%%", 
                     attempt, stddev_percent, SAMPLE_STABILITY_THRESHOLD);
        }
        
    retry_delay:
        if (attempt < max_retry) {
            IRR_LOGI("2秒后重试...");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
    
    IRR_LOGE("采样失败：连续 %d 次尝试均不稳定", max_retry);
    return false;
}

// 浇水任务函数
static void watering_task(void *arg) {
    uint32_t duration_ms = (uint32_t)(uintptr_t)arg;  // 从指针转换回毫秒
    vTaskDelay(duration_ms / portTICK_PERIOD_MS);
    
    // 停止浇水
    gpio_control_set_level(s_config.pump_pin, false);
    s_state.is_watering = false;
    
    IRR_LOGI("浇水完成，本次浇水 %.1f秒", duration_ms / 1000.0f);
    IRR_LOGI("本周已浇水次数: %d/%d", 
            s_state.week_water_count, s_config.week_max_times);
    
    vTaskDelete(NULL);
}

// 执行浇水操作
static void start_watering(void) {
    if (s_state.is_watering) {
        IRR_LOGW("已经在浇水状态");
        return;
    }
    
    s_state.is_watering = true;
    s_state.week_water_count++;
    s_state.last_water_time = time_manager_get_unix_time();
    
    gpio_control_set_level(s_config.pump_pin, true);
    IRR_LOGI("开始浇水，时长: %.1f秒", s_config.water_duration);
    
    // 将浮点秒数转换为毫秒整数，通过任务参数传递
    uint32_t duration_ms = (uint32_t)(s_config.water_duration * 1000.0f);
    xTaskCreate(
        watering_task,
        "watering_task",
        3072,                     // 增大栈大小
        (void*)(uintptr_t)duration_ms,  // 直接传递整数值
        2,
        NULL
    );
}

// 补浇水任务函数
static void makeup_watering_task(void *arg) {
    uint32_t total_duration_ms = (uint32_t)(uintptr_t)arg;
    vTaskDelay(total_duration_ms / portTICK_PERIOD_MS);
    
    gpio_control_set_level(s_config.pump_pin, false);
    IRR_LOGI("补浇水完成，总时长 %.1f秒", total_duration_ms / 1000.0f);
    
    vTaskDelete(NULL);
}

// 传感器检查任务函数（实际采样与浇水判断）
static void sensor_check_task(void *arg) {
    // 等待传感器稳定（2秒）
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // 采样湿度（最多重试3次）
    float humidity = 0;
    bool stable = sample_stable_humidity(&humidity, 3);
    
    if (stable) {
        // 判断是否浇水
        if (should_water_now((int)humidity)) {
            start_watering();
        } else {
            IRR_LOGI("当前湿度 %.1f%%，无需浇水", humidity);
        }
    } else {
        IRR_LOGE("湿度采样不稳定，放弃本次浇水判断");
    }
    
    // 关闭传感器电源
    stop_sensor_power();
    
    vTaskDelete(NULL);
}

// 处理周最小浇水次数逻辑
static void handle_week_minimum_watering(void) {
    if (is_new_week()) {
        IRR_LOGI("新的一周开始，ISO周数: %d", s_state.current_week);
        
        int last_week_count = s_state.week_water_count;
        int required_min = s_config.week_min_times;
        
        if (last_week_count < required_min) {
            int deficit = required_min - last_week_count;
            float total_duration = deficit * s_config.water_duration;
            
            IRR_LOGI("上周浇水 %d 次，未达到最小 %d 次，补浇 %d 次，总时长 %.1f秒",
                    last_week_count, required_min, deficit, total_duration);
            
            s_state.week_water_count = 0;
            gpio_control_set_level(s_config.pump_pin, true);
            
            uint32_t total_duration_ms = (uint32_t)(total_duration * 1000.0f);
            xTaskCreate(
                makeup_watering_task,
                "makeup_watering_task",
                3072,                     // 同样增大栈
                (void*)(uintptr_t)total_duration_ms,
                2,
                NULL
            );
        } else {
            IRR_LOGI("上周浇水 %d 次，达到最小 %d 次，无需补浇",
                    last_week_count, required_min);
            s_state.week_water_count = 0;
        }
    }
}

// 每4小时检查一次是否需要浇水
static void check_watering_schedule(void) {
    time_t now = time_manager_get_unix_time();
    
    // 每4小时检查一次（14400秒）
    if (difftime(now, s_state.last_check_time) >= 14400.0) {
        s_state.last_check_time = now;
        
        IRR_LOGI("4小时检查点，准备检查土壤湿度");
        
        // 启动传感器电源
        start_sensor_power();
        
        // 2秒后开始采样（已在任务中延迟），创建传感器检查任务
        xTaskCreate(
            sensor_check_task,
            "sensor_check_task",
            4096,          // 堆栈稍大，因为包含采样和数据处理
            NULL,
            2,
            NULL
        );
    }
}

// 初始化浇水控制模块
void irrigation_controller_init(int pump_pin) {
    s_config.pump_pin = pump_pin;
    
    // 获取当前ISO周数（直接调用time_manager）
    s_state.current_week = time_manager_get_current_iso_week();
    
    s_state.last_check_time = time_manager_get_unix_time();
    
    IRR_LOGI("浇水控制模块初始化完成");
    IRR_LOGI("水泵控制引脚: GPIO%d", pump_pin);
    IRR_LOGI("当前ISO周数: %d", s_state.current_week);
    IRR_LOGI("默认设置: 阈值 %d%%, 时长 %.1fs, 周最小 %d 次, 周最大 %d 次",
            s_config.trigger_threshold, s_config.water_duration,
            s_config.week_min_times, s_config.week_max_times);
}

// 浇水控制轮询函数
void irrigation_controller_poll(void) {
    TickType_t current_ticks = xTaskGetTickCount();
    
    if ((current_ticks - s_last_poll_ticks) >= s_poll_interval_ticks) {
        // 先处理时间重置（无论测试模式）
        check_time_reset();

        if (!s_state.test_mode) {
            // 处理周最小浇水逻辑
            handle_week_minimum_watering();

            // 强制检查（时间重置后立即触发一次）
            if (s_state.force_check_needed) {
                IRR_LOGI("强制浇水检查（时间重置后）");
                start_sensor_power();
                xTaskCreate(
                    sensor_check_task,
                    "sensor_check_task",
                    4096,
                    NULL,
                    2,
                    NULL
                );
                s_state.force_check_needed = false;
                
                // 强制检查后，将上次检查时间设置为当前时间，
                // 避免紧接着的4小时检查立即触发
                s_state.last_check_time = time_manager_get_unix_time();
            } 
            // 正常4小时周期检查
            else {
                check_watering_schedule();
            }
        }
        
        s_last_poll_ticks = current_ticks;
    }
}

// 设置触发阈值
bool irrigation_controller_set_threshold(int threshold) {
    if (threshold < 0 || threshold > 100) {
        IRR_LOGE("阈值超出范围: %d (0-100)", threshold);
        return false;
    }
    
    s_config.trigger_threshold = threshold;
    IRR_LOGI("触发阈值设置为: %d%%", threshold);
    return true;
}

// 设置单次浇水时长
bool irrigation_controller_set_duration(float seconds) {
    if (seconds <= 0) {
        IRR_LOGE("浇水时长必须为正数: %.1f", seconds);
        return false;
    }
    
    s_config.water_duration = seconds;
    IRR_LOGI("单次浇水时长设置为: %.1f秒", seconds);
    return true;
}

// 设置周最小浇水次数
bool irrigation_controller_set_week_min(int min_times) {
    if (min_times < 0) {
        IRR_LOGE("周最小浇水次数不能为负数: %d", min_times);
        return false;
    }
    
    if (min_times > s_config.week_max_times) {
        IRR_LOGW("周最小浇水次数 %d 大于周最大浇水次数 %d", 
                min_times, s_config.week_max_times);
    }
    
    s_config.week_min_times = min_times;
    IRR_LOGI("周最小浇水次数设置为: %d", min_times);
    return true;
}

// 设置周最大浇水次数
bool irrigation_controller_set_week_max(int max_times) {
    if (max_times < 0) {
        IRR_LOGE("周最大浇水次数不能为负数: %d", max_times);
        return false;
    }
    
    if (max_times < s_config.week_min_times) {
        IRR_LOGW("周最大浇水次数 %d 小于周最小浇水次数 %d", 
                max_times, s_config.week_min_times);
    }
    
    s_config.week_max_times = max_times;
    IRR_LOGI("周最大浇水次数设置为: %d", max_times);
    return true;
}

// 获取当前设置
int irrigation_controller_get_threshold(void) {
    return s_config.trigger_threshold;
}

float irrigation_controller_get_duration(void) {
    return s_config.water_duration;
}

int irrigation_controller_get_week_min(void) {
    return s_config.week_min_times;
}

int irrigation_controller_get_week_max(void) {
    return s_config.week_max_times;
}

int irrigation_controller_get_week_count(void) {
    return s_state.week_water_count;
}

// 获取传感器启动信号状态
bool irrigation_controller_get_sensor_power_status(void) {
    return s_state.sensor_power;
}

// 重置浇水次数
bool irrigation_controller_reset_week(void) {
    s_state.week_water_count = 0;
    IRR_LOGI("本周浇水次数已重置为0");
    return true;
}

// 通知时间重置（当系统时间被设置时调用）
void irrigation_controller_notify_time_reset(void) {
    s_state.time_reset_needed = true;
    IRR_LOGI("时间重置通知已接收");
}