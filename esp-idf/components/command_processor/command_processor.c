#include "command_processor.h"
#include "esp_log.h"
#include "light_control.h"
#include "time_manager.h"
#include "ventilation_control.h" 
#include "irrigation_controller.h"
#include "moisture_sensor.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CMD_PROCESSOR";

// 模块信息结构体
typedef struct {
    char name[16];      // 模块名称
    const char* description; // 模块描述
    int pin;           // 控制引脚
} module_info_t;

// 预定义的模块列表
static module_info_t s_module_list[] = {
    {"fan", "通风控制", 1},      // 通风控制，GPIO1
    {"light", "补光灯控制", 15/14}, // 补光灯控制，GPIO15
    {"pump", "浇水控制", 5},
};

static const int s_module_count = sizeof(s_module_list) / sizeof(s_module_list[0]);

// 打印完整命令帮助
static void print_command_help(void) {
    ESP_LOGI(TAG, "=== 完整命令帮助 ===");
    
    // 系统命令部分
    ESP_LOGI(TAG, "--- 系统命令 ---");
    ESP_LOGI(TAG, "help               - 显示此帮助");
    ESP_LOGI(TAG, "module list        - 列出所有控制模块");
    
    // 浇水控制部分
    ESP_LOGI(TAG, "--- 浇水控制 ---");
    ESP_LOGI(TAG, "irrigation set threshold <0-100> - 设置触发阈值");
    ESP_LOGI(TAG, "irrigation set duration <秒>   - 设置单次浇水时长");
    ESP_LOGI(TAG, "irrigation set week_min <次数> - 设置周最小浇水次数");
    ESP_LOGI(TAG, "irrigation set week_max <次数> - 设置周最大浇水次数");
    ESP_LOGI(TAG, "irrigation manual trigger      - 手动触发浇水");
    ESP_LOGI(TAG, "irrigation test mode on/off    - 开启/关闭测试模式");
    ESP_LOGI(TAG, "irrigation reset week          - 重置本周浇水次数");
    ESP_LOGI(TAG, "irrigation status              - 显示浇水状态");
    
    // 补光灯控制部分
    ESP_LOGI(TAG, "--- 补光灯控制 ---");
    ESP_LOGI(TAG, "light set start HH:MM  - 设置开启时间");
    ESP_LOGI(TAG, "light set end HH:MM    - 设置关闭时间");
    ESP_LOGI(TAG, "light set duration X.X - 设置照明时长(小时)");
    ESP_LOGI(TAG, "light on               - 手动开启");
    ESP_LOGI(TAG, "light off              - 手动关闭");
    ESP_LOGI(TAG, "light auto             - 切换为自动模式");
    ESP_LOGI(TAG, "light status           - 显示状态");
    
    // 时间管理部分
    ESP_LOGI(TAG, "--- 时间管理 ---");
    ESP_LOGI(TAG, "time                  - 显示当前时间");
    ESP_LOGI(TAG, "time get              - 显示当前时间");
    ESP_LOGI(TAG, "time set Y/M/D H:M:S  - 设置系统时间");

    // 土壤湿度传感器部分
    ESP_LOGI(TAG, "--- 土壤湿度传感器 ---");
    ESP_LOGI(TAG, "moisture on            - 打开传感器电源(GPIO15)");
    ESP_LOGI(TAG, "moisture off           - 关闭传感器电源");
    ESP_LOGI(TAG, "moisture read          - 开始连续采集(每秒打印电压)");
    ESP_LOGI(TAG, "moisture stop          - 停止连续采集");
    ESP_LOGI(TAG, "moisture <XX>          - 设置模拟湿度值(用于浇水测试)");
    
    // 示例部分
    ESP_LOGI(TAG, "--- 示例 ---");
    ESP_LOGI(TAG, "浇水: moisture 45, irrigation set threshold 60");
    ESP_LOGI(TAG, "补光灯: light set start 08:30, light auto");
    ESP_LOGI(TAG, "时间: time set 2025/02/05 14:30:00");
    ESP_LOGI(TAG, "=========================");
}

// 处理模块列表命令
static void handle_module_list(void) {
    ESP_LOGI(TAG, "=== 模块列表 ===");
    ESP_LOGI(TAG, "模块        状态     引脚");
    ESP_LOGI(TAG, "---------   -------  --------");
    
    // 通风控制模块（fan）
    bool fan_state = ventilation_control_get_state();
    int fan_pin = ventilation_control_get_pin();
    ESP_LOGI(TAG, "%-12s %-8s  GPIO%d",
             "fan",
             fan_state ? "ON" : "OFF",
             fan_pin);
    
    // 补光灯控制模块（light）
    bool light_state = light_control_is_on();
    int light_pin = light_control_get_pin();
    int pwm_pin = light_control_get_pwm_pin();
    ESP_LOGI(TAG, "%-12s %-8s  GPIO%d/GPIO%d",
             "light",
             light_state ? "ON" : "OFF",
             light_pin, pwm_pin);
    
    // 浇水控制模块（irrigation）
    int irrigation_pin = 15;  // GPIO15
    bool sensor_power = irrigation_controller_get_sensor_power_status();
    ESP_LOGI(TAG, "%-12s %-8s  GPIO%d",
             "irrigation",
             sensor_power ? "SENSOR" : "IDLE",
             irrigation_pin);
    
    ESP_LOGI(TAG, "===================");
}

// 处理土壤湿度传感器命令
static void handle_moisture_command(const char* command) {
    // 命令格式：moisture <参数>
    const char* sub = command + 9;  // 跳过 "moisture "

    // ---- 传感器电源控制 ----
    if (strcmp(sub, "on") == 0) {
        moisture_sensor_power_on();
    }
    else if (strcmp(sub, "off") == 0) {
        moisture_sensor_power_off();
    }
    // ---- 连续读取控制 ----
    else if (strcmp(sub, "read") == 0) {
        moisture_sensor_start_reading();
    }
    else if (strcmp(sub, "stop") == 0) {
        moisture_sensor_stop_reading();
    }
    // ---- 原有的模拟湿度设置（用于浇水测试）----
    else {
        int moisture;
        if (sscanf(sub, "%d", &moisture) == 1) {
            irrigation_controller_set_moisture_sim(moisture);
        } else {
            ESP_LOGI(TAG, "格式错误，正确格式: moisture <两位数字> 或 moisture on/off/read/stop");
            ESP_LOGI(TAG, "示例: moisture 45, moisture on, moisture read");
        }
    }
}

// 处理浇水控制命令
static void handle_irrigation_command(const char* command) {
    // 检查是否为带"irrigation "前缀的命令
    if (strncmp(command, "irrigation ", 11) == 0) {
        const char* sub_command = command + 11;
        
        // 处理 irrigation set threshold 命令
        if (strncmp(sub_command, "set threshold ", 14) == 0) {
            int threshold;
            if (sscanf(sub_command + 14, "%d", &threshold) == 1) {
                irrigation_controller_set_threshold(threshold);
            } else {
                ESP_LOGI(TAG, "格式错误，正确格式: irrigation set threshold <0-100>");
            }
        }
        // 处理 irrigation set duration 命令
        else if (strncmp(sub_command, "set duration ", 13) == 0) {
            float duration;
            if (sscanf(sub_command + 13, "%f", &duration) == 1) {
                irrigation_controller_set_duration(duration);
            } else {
                ESP_LOGI(TAG, "格式错误，正确格式: irrigation set duration <秒数>");
            }
        }
        // 处理 irrigation set week_min 命令
        else if (strncmp(sub_command, "set week_min ", 13) == 0) {
            int min_times;
            if (sscanf(sub_command + 13, "%d", &min_times) == 1) {
                irrigation_controller_set_week_min(min_times);
            } else {
                ESP_LOGI(TAG, "格式错误，正确格式: irrigation set week_min <次数>");
            }
        }
        // 处理 irrigation set week_max 命令
        else if (strncmp(sub_command, "set week_max ", 13) == 0) {
            int max_times;
            if (sscanf(sub_command + 13, "%d", &max_times) == 1) {
                irrigation_controller_set_week_max(max_times);
            } else {
                ESP_LOGI(TAG, "格式错误，正确格式: irrigation set week_max <次数>");
            }
        }
        // 处理 irrigation manual trigger 命令
        else if (strcmp(sub_command, "manual trigger") == 0) {
            irrigation_controller_manual_trigger();
        }
        // 处理 irrigation test mode on 命令
        else if (strcmp(sub_command, "test mode on") == 0) {
            ESP_LOGI(TAG, "测试模式已开启，跳过4小时限制");
            // 注意：test_mode现在在irrigation_controller内部管理
        }
        // 处理 irrigation test mode off 命令
        else if (strcmp(sub_command, "test mode off") == 0) {
            ESP_LOGI(TAG, "测试模式已关闭");
        }
        // 处理 irrigation reset week 命令
        else if (strcmp(sub_command, "reset week") == 0) {
            irrigation_controller_reset_week();
        }
        // 处理 irrigation status 命令
        else if (strcmp(sub_command, "status") == 0) {
            int threshold = irrigation_controller_get_threshold();
            float duration = irrigation_controller_get_duration();
            int week_min = irrigation_controller_get_week_min();
            int week_max = irrigation_controller_get_week_max();
            int week_count = irrigation_controller_get_week_count();
            
            ESP_LOGI(TAG, "=== 浇水控制状态 ===");
            ESP_LOGI(TAG, "触发阈值: %d%%", threshold);
            ESP_LOGI(TAG, "单次浇水时长: %.1f秒", duration);
            ESP_LOGI(TAG, "周最小浇水次数: %d", week_min);
            ESP_LOGI(TAG, "周最大浇水次数: %d", week_max);
            ESP_LOGI(TAG, "本周已浇水次数: %d/%d", week_count, week_max);
            ESP_LOGI(TAG, "传感器电源状态: %s", 
                    irrigation_controller_get_sensor_power_status() ? "开启" : "关闭");
            ESP_LOGI(TAG, "================");
        }
        else {
            ESP_LOGI(TAG, "未知的浇水控制命令，输入 'help' 查看帮助");
        }
    }
    else {
        ESP_LOGI(TAG, "未知的浇水命令，输入 'help' 查看帮助");
    }
}

// 处理补光灯命令
static void handle_light_command(const char* command) {
    if (strncmp(command, "light set start ", 16) == 0) {
        // 格式: light set start HH:MM
        int hour, minute;
        if (sscanf(command + 16, "%d:%d", &hour, &minute) == 2) {
            if (light_control_set_start_time(hour, minute)) {
                ESP_LOGI(TAG, "补光灯开启时间设置成功");
            }
        } else {
            ESP_LOGI(TAG, "格式错误，正确格式: light set start HH:MM");
        }
    }
    else if (strncmp(command, "light set end ", 14) == 0) {
        // 格式: light set end HH:MM
        int hour, minute;
        if (sscanf(command + 14, "%d:%d", &hour, &minute) == 2) {
            if (light_control_set_end_time(hour, minute)) {
                ESP_LOGI(TAG, "补光灯关闭时间设置成功");
            }
        } else {
            ESP_LOGI(TAG, "格式错误，正确格式: light set end HH:MM");
        }
    }
    else if (strncmp(command, "light set duration ", 19) == 0) {
        // 格式: light set duration X.X
        float hours;
        if (sscanf(command + 19, "%f", &hours) == 1) {
            if (light_control_set_duration(hours)) {
                ESP_LOGI(TAG, "补光灯照明时长设置成功");
            }
        } else {
            ESP_LOGI(TAG, "格式错误，正确格式: light set duration X.X");
        }
    }
    else if (strcmp(command, "light on") == 0) {
        // 手动开启
        light_control_manual_set(true);
    }
    else if (strcmp(command, "light off") == 0) {
        // 手动关闭
        light_control_manual_set(false);
    }
    else if (strcmp(command, "light auto") == 0) {
        // 切回自动模式
        light_control_set_auto_mode();
    }
    else if (strcmp(command, "light status") == 0) {
        int start_hour = light_control_get_start_hour();
        int start_minute = light_control_get_start_minute();
        int end_hour = light_control_get_end_hour();
        int end_minute = light_control_get_end_minute();
        float duration = light_control_get_duration();
        uint8_t pwm_duty = light_control_get_pwm_duty();
        light_state_t state = light_control_get_state();
        
        // 显示状态
        ESP_LOGI(TAG, "=== 补光灯状态 ===");
        ESP_LOGI(TAG, "开启时间: %02d:%02d", start_hour, start_minute);
        ESP_LOGI(TAG, "关闭时间: %02d:%02d", end_hour, end_minute);
        ESP_LOGI(TAG, "照明时长: %.1f小时", duration);
        ESP_LOGI(TAG, "当前状态: %s", 
                 state == LIGHT_STATE_OFF ? "关闭" : 
                 state == LIGHT_STATE_ON ? "开启(开关模式)" : "开启(PWM模式)");
        ESP_LOGI(TAG, "PWM占空比: %d%%", pwm_duty);
        ESP_LOGI(TAG, "模式: %s", light_control_is_manual_mode() ? "手动" : "自动");
        ESP_LOGI(TAG, "开关引脚: GPIO%d", light_control_get_pin());
        ESP_LOGI(TAG, "PWM引脚: GPIO%d", light_control_get_pwm_pin());
        ESP_LOGI(TAG, "================");
    }
    else {
        ESP_LOGI(TAG, "未知的补光灯命令，输入 'help' 查看帮助");
    }
}

// 处理时间命令
static void handle_time_command(const char* command) {
    // 检查是否为时间设置命令
    if (strncmp(command, "time set ", 9) == 0) {
        int year, month, day, hour, minute, second;
        
        // 解析格式: time set YYYY/MM/DD HH:MM:SS
        int result = sscanf(command + 9, "%d/%d/%d %d:%d:%d",
                           &year, &month, &day, &hour, &minute, &second);
        
        if (result == 6) {
            irrigation_controller_notify_time_reset(); // 通知浇水控制模块时间被重置
            if (time_manager_set_time(year, month, day, hour, minute, second)) {
                ESP_LOGI(TAG, "时间设置成功: %s", time_manager_get_time_string());
            } else {
                ESP_LOGI(TAG, "时间设置失败，请检查参数");
            }
        } else {
            ESP_LOGI(TAG, "时间格式错误");
            ESP_LOGI(TAG, "正确格式: time set YYYY/MM/DD HH:MM:SS");
            ESP_LOGI(TAG, "示例: time set 2025/02/05 14:30:00");
        }
    } else if (strcmp(command, "time") == 0 || strcmp(command, "time get") == 0) {
        // 查询当前时间
        ESP_LOGI(TAG, "当前时间: %s", time_manager_get_time_string());
    } else {
        ESP_LOGI(TAG, "未知的时间命令，输入 'help' 查看帮助");
    }
}

// 初始化函数
void command_processor_init(void) {
    ESP_LOGI(TAG, "命令处理器初始化完成");
    ESP_LOGI(TAG, "系统模块数量: %d", s_module_count);
    print_command_help();
}

// 处理输入帧的核心函数
void command_processor_process_frame(const char* frame) {
    if (frame == NULL) return;

    ESP_LOGI(TAG, "处理命令: %s", frame);

    // --- 处理 help 命令 ---
    if (strcmp(frame, "help") == 0) {
        print_command_help();
        return;
    }

    // --- 处理 module list 命令 ---
    if (strcmp(frame, "module list") == 0) {
        handle_module_list();
        return;
    }

    // --- 处理补光灯命令 ---
    if (strncmp(frame, "light", 5) == 0) {
        handle_light_command(frame);
        return;
    }

    // --- 处理时间命令 ---
    if (strncmp(frame, "time", 4) == 0) {
        handle_time_command(frame);
        return;
    }

    // --- 处理浇水控制命令 ---
    if (strncmp(frame, "irrigation", 10) == 0) {
        handle_irrigation_command(frame);
        return;
    }

    // 土壤湿度传感器命令（moisture 前缀）
    if (strncmp(frame, "moisture", 8) == 0) {
        handle_moisture_command(frame);
        return;
    }

    // 未知命令
    ESP_LOGI(TAG, "未知命令，输入 'help' 查看可用命令");
}