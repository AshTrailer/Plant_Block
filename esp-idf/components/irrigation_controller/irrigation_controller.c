#include "irrigation_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time_manager.h"
#include "gpio_control.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "IRRIGATION";

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
    int sim_moisture;       // 模拟土壤湿度值（测试用）
    bool test_mode;         // 测试模式（跳过4小时限制）
} irrigation_state_t;

static irrigation_config_t s_config = {
    .pump_pin = 15,
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
    .sim_moisture = 0,
    .test_mode = false,
};

static TickType_t s_last_poll_ticks = 0;
static const TickType_t s_poll_interval_ticks = 1000 / portTICK_PERIOD_MS; // 1秒

// 任务函数声明
static void watering_task(void *arg);
static void makeup_watering_task(void *arg);
static void sensor_check_task(void *arg);

// ISO8601周数计算函数
static int get_iso_week_number(int year, int month, int day) {
    // 简化版ISO8601周数计算
    // 这里使用一个简化的实现，实际应用可能需要更精确的计算
    // 或者使用标准库函数
    
    // 计算该日期是一年中的第几天
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    // 闰年检查
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[1] = 29;
    }
    
    int day_of_year = day;
    for (int i = 0; i < month - 1; i++) {
        day_of_year += days_in_month[i];
    }
    
    // 计算1月1日是星期几
    // 使用Zeller's congruence算法计算星期
    int y = year;
    int m = month;
    int d = day;
    
    if (m < 3) {
        m += 12;
        y--;
    }
    
    int century = y / 100;
    int year_of_century = y % 100;
    
    int weekday = (d + (13 * (m + 1)) / 5 + year_of_century + 
                   year_of_century / 4 + century / 4 - 2 * century) % 7;
    
    // 调整结果为0=星期六，1=星期日，...，6=星期五
    weekday = (weekday + 6) % 7;
    
    // 计算ISO周数
    // 第一周是包含1月4日的周
    int iso_week = (day_of_year - weekday + 10) / 7;
    
    // 处理边界情况
    if (iso_week < 1) {
        // 上一年的最后一周
        iso_week = 52;
    } else if (iso_week > 52) {
        // 下一年的第一周
        iso_week = 1;
    }
    
    return iso_week;
}

// 检查是否为新的一周（周一00:00）
static bool is_new_week(void) {
    system_time_t current_time = time_manager_get_time();
    int current_weekday = time_manager_get_weekday();
    
    // 周一（weekday=1）且时间为00:00:00
    if (current_weekday == 1 && 
        current_time.hour == 0 && 
        current_time.minute == 0 && 
        current_time.second == 0) {
        
        // 计算当前ISO周数
        int iso_week = get_iso_week_number(
            current_time.year, 
            current_time.month, 
            current_time.day
        );
        
        // 如果周数不同，则是新的一周
        if (iso_week != s_state.current_week) {
            s_state.current_week = iso_week;
            return true;
        }
    }
    
    return false;
}

// 启动传感器电源
static void start_sensor_power(void) {
    s_state.sensor_power = true;
    ESP_LOGI(TAG, "传感器电源已开启");
    // 这里可以添加实际的GPIO控制代码
    // gpio_control_set_level(sensor_power_pin, true);
}

// 关闭传感器电源
static void stop_sensor_power(void) {
    s_state.sensor_power = false;
    ESP_LOGI(TAG, "传感器电源已关闭");
    // 这里可以添加实际的GPIO控制代码
    // gpio_control_set_level(sensor_power_pin, false);
}

// 检查是否需要浇水（核心逻辑）
static bool should_water_now(int moisture_value) {
    // 1. 检查土壤湿度是否低于阈值
    if (moisture_value >= s_config.trigger_threshold) {
        ESP_LOGI(TAG, "土壤湿度 %d%% 高于阈值 %d%%，不浇水", 
                moisture_value, s_config.trigger_threshold);
        return false;
    }
    
    // 2. 检查4小时内是否已经浇过水（测试模式下跳过）
    if (!s_state.test_mode) {
        if (s_state.last_water_time > 0) {
            time_t now = time_manager_get_unix_time();  // 使用time_manager的Unix时间戳
            double hours_since_last = difftime(now, s_state.last_water_time) / 3600.0;
            
            if (hours_since_last < 4.0) {
                ESP_LOGI(TAG, "距离上次浇水 %.1f 小时，不足4小时，不浇水", hours_since_last);
                return false;
            }
        }
    }
    
    // 3. 检查本周浇水次数是否超过最大值
    if (s_state.week_water_count >= s_config.week_max_times) {
        ESP_LOGI(TAG, "本周已浇水 %d 次，已达最大值 %d 次，不浇水",
                s_state.week_water_count, s_config.week_max_times);
        return false;
    }
    
    // 所有条件满足，可以浇水
    ESP_LOGI(TAG, "触发浇水条件: 湿度 %d%% < 阈值 %d%%", 
            moisture_value, s_config.trigger_threshold);
    return true;
}

// 浇水任务函数
static void watering_task(void *arg) {
    float duration = *(float *)arg;
    
    vTaskDelay(duration * 1000 / portTICK_PERIOD_MS);
    
    // 停止浇水
    gpio_control_set_level(s_config.pump_pin, false);
    s_state.is_watering = false;
    
    ESP_LOGI(TAG, "浇水完成，本次浇水 %.1f秒", duration);
    ESP_LOGI(TAG, "本周已浇水次数: %d/%d", 
            s_state.week_water_count, s_config.week_max_times);
    
    vTaskDelete(NULL);
}

// 执行浇水操作
static void start_watering(void) {
    if (s_state.is_watering) {
        ESP_LOGW(TAG, "已经在浇水状态");
        return;
    }
    
    s_state.is_watering = true;
    s_state.week_water_count++;
    s_state.last_water_time = time_manager_get_unix_time();  // 记录当前Unix时间
    
    // 开启水泵
    gpio_control_set_level(s_config.pump_pin, true);
    ESP_LOGI(TAG, "开始浇水，时长: %.1f秒", s_config.water_duration);
    
    // 创建浇水停止任务
    float duration = s_config.water_duration;
    xTaskCreate(
        watering_task,
        "watering_task",
        2048,
        &duration,
        2,
        NULL
    );
}

// 补浇水任务函数
static void makeup_watering_task(void *arg) {
    float total_duration = *(float *)arg;
    
    vTaskDelay(total_duration * 1000 / portTICK_PERIOD_MS);
    
    gpio_control_set_level(s_config.pump_pin, false);
    ESP_LOGI(TAG, "补浇水完成，总时长 %.1f秒", total_duration);
    
    vTaskDelete(NULL);
}

// 处理周最小浇水次数逻辑
static void handle_week_minimum_watering(void) {
    if (is_new_week()) {
        ESP_LOGI(TAG, "新的一周开始，ISO周数: %d", s_state.current_week);
        
        // 检查上周是否达到最小浇水次数
        int last_week_count = s_state.week_water_count;
        int required_min = s_config.week_min_times;
        
        if (last_week_count < required_min) {
            int deficit = required_min - last_week_count;
            float total_duration = deficit * s_config.water_duration;
            
            ESP_LOGI(TAG, "上周浇水 %d 次，未达到最小 %d 次，补浇 %d 次，总时长 %.1f秒",
                    last_week_count, required_min, deficit, total_duration);
            
            // 补浇水（不计入本周计数）
            s_state.week_water_count = 0; // 重置本周计数
            gpio_control_set_level(s_config.pump_pin, true);
            
            // 创建补浇水任务
            xTaskCreate(
                makeup_watering_task,
                "makeup_watering_task",
                2048,
                &total_duration,
                2,
                NULL
            );
        } else {
            ESP_LOGI(TAG, "上周浇水 %d 次，达到最小 %d 次，无需补浇",
                    last_week_count, required_min);
            s_state.week_water_count = 0; // 重置本周计数
        }
    }
}

// 传感器检查任务函数
static void sensor_check_task(void *arg) {
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // 这里应该读取实际传感器值
    // 目前使用模拟值
    int moisture = s_state.sim_moisture;
    
    ESP_LOGI(TAG, "读取土壤湿度: %d%%", moisture);
    
    if (should_water_now(moisture)) {
        start_watering();
    }
    
    // 关闭传感器电源
    stop_sensor_power();
    
    vTaskDelete(NULL);
}

// 每4小时检查一次是否需要浇水
static void check_watering_schedule(void) {
    time_t now = time_manager_get_unix_time();  // 使用time_manager的Unix时间戳
    
    // 每4小时检查一次（14400秒）
    if (difftime(now, s_state.last_check_time) >= 14400.0) {
        s_state.last_check_time = now;
        
        ESP_LOGI(TAG, "4小时检查点，准备检查土壤湿度");
        
        // 提前2秒启动传感器
        start_sensor_power();
        
        // 2秒后检查湿度
        xTaskCreate(
            sensor_check_task,
            "sensor_check_task",
            2048,
            NULL,
            2,
            NULL
        );
    }
}

// 初始化浇水控制模块
void irrigation_controller_init(int pump_pin) {
    s_config.pump_pin = pump_pin;
    
    // 获取当前周数
    system_time_t current_time = time_manager_get_time();
    s_state.current_week = get_iso_week_number(
        current_time.year, 
        current_time.month, 
        current_time.day
    );
    
    s_state.last_check_time = time_manager_get_unix_time();  // 初始化检查时间
    
    ESP_LOGI(TAG, "浇水控制模块初始化完成");
    ESP_LOGI(TAG, "水泵控制引脚: GPIO%d", pump_pin);
    ESP_LOGI(TAG, "当前ISO周数: %d", s_state.current_week);
    ESP_LOGI(TAG, "默认设置: 阈值 %d%%, 时长 %.1fs, 周最小 %d 次, 周最大 %d 次",
            s_config.trigger_threshold, s_config.water_duration,
            s_config.week_min_times, s_config.week_max_times);
}

// 浇水控制轮询函数
void irrigation_controller_poll(void) {
    TickType_t current_ticks = xTaskGetTickCount();
    
    if ((current_ticks - s_last_poll_ticks) >= s_poll_interval_ticks) {
        if (!s_state.test_mode) {
            // 正常模式：处理周最小浇水逻辑和4小时检查
            handle_week_minimum_watering();
            check_watering_schedule();
        }
        
        s_last_poll_ticks = current_ticks;
    }
}

// 模拟土壤湿度输入（用于测试）
void irrigation_controller_set_moisture_sim(int moisture) {
    if (moisture < 0) moisture = 0;
    if (moisture > 100) moisture = 100;
    
    s_state.sim_moisture = moisture;
    ESP_LOGI(TAG, "设置模拟土壤湿度: %d%%", moisture);
}

// 设置触发阈值
bool irrigation_controller_set_threshold(int threshold) {
    if (threshold < 0 || threshold > 100) {
        ESP_LOGE(TAG, "阈值超出范围: %d (0-100)", threshold);
        return false;
    }
    
    s_config.trigger_threshold = threshold;
    ESP_LOGI(TAG, "触发阈值设置为: %d%%", threshold);
    return true;
}

// 设置单次浇水时长
bool irrigation_controller_set_duration(float seconds) {
    if (seconds <= 0) {
        ESP_LOGE(TAG, "浇水时长必须为正数: %.1f", seconds);
        return false;
    }
    
    s_config.water_duration = seconds;
    ESP_LOGI(TAG, "单次浇水时长设置为: %.1f秒", seconds);
    return true;
}

// 设置周最小浇水次数
bool irrigation_controller_set_week_min(int min_times) {
    if (min_times < 0) {
        ESP_LOGE(TAG, "周最小浇水次数不能为负数: %d", min_times);
        return false;
    }
    
    if (min_times > s_config.week_max_times) {
        ESP_LOGW(TAG, "周最小浇水次数 %d 大于周最大浇水次数 %d", 
                min_times, s_config.week_max_times);
    }
    
    s_config.week_min_times = min_times;
    ESP_LOGI(TAG, "周最小浇水次数设置为: %d", min_times);
    return true;
}

// 设置周最大浇水次数
bool irrigation_controller_set_week_max(int max_times) {
    if (max_times < 0) {
        ESP_LOGE(TAG, "周最大浇水次数不能为负数: %d", max_times);
        return false;
    }
    
    if (max_times < s_config.week_min_times) {
        ESP_LOGW(TAG, "周最大浇水次数 %d 小于周最小浇水次数 %d", 
                max_times, s_config.week_min_times);
    }
    
    s_config.week_max_times = max_times;
    ESP_LOGI(TAG, "周最大浇水次数设置为: %d", max_times);
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

// 手动触发浇水（测试用）
void irrigation_controller_manual_trigger(void) {
    ESP_LOGI(TAG, "手动触发浇水");
    s_state.test_mode = true;
    
    // 模拟低湿度触发
    if (should_water_now(30)) {
        start_watering();
    }
    
    s_state.test_mode = false;
}

// 获取传感器启动信号状态
bool irrigation_controller_get_sensor_power_status(void) {
    return s_state.sensor_power;
}

// 重置浇水次数
bool irrigation_controller_reset_week(void) {
    s_state.week_water_count = 0;
    ESP_LOGI(TAG, "本周浇水次数已重置为0");
    return true;
}