#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "esp_system.h" 

#include "time_manager.h"
#include "input_parser.h"
#include "command_processor.h"
#include "gpio_control.h"
#include "ventilation_control.h"
#include "light_control.h"
#include "irrigation_controller.h"
#include "moisture_sensor.h"
#include "cloud_comm.h"
#include "hmi_uart.h"
#include "ds18b20_sensor.h"
#include "sht30_sensor.h"
#include "float_switch.h"



#define MOISTURE_SENSOR_POWER_PIN   15   // GPIO15 → 土壤湿度传感器 VCC 控制
#define MOISTURE_SENSOR_ADC_PIN     35   // GPIO35 → 土壤湿度模拟输入 (ADC)
#define VENTILATION_CONTROL_PIN     13   // GPIO13 → 通风风扇 (NMOS)
#define IRRIGATION_CONTROL_PIN      12   // GPIO12 → 蠕动泵 (NMOS)
#define LIGHT_PWM_PIN               14   // GPIO14 → 升压恒流光照 PWM
#define DS18B20_BUS_GPIO            16   // GPIO16 → DS18B20 #1/#2
#define DS18B20_MAX_COUNT           2    // 挂 2 个传感器
#define SHT30_SDA_PIN               21   // GPIO21 → SHT30 SDA
#define SHT30_SCL_PIN               22   // GPIO22 → SHT30 SCL

// ==================== 系统固定引脚 ====================
// GPIO0  → 自动下载电路 + 按键 (strapping, 不可更改)
// GPIO1  → UART TXD0 → CH340 RX (固定)
// GPIO3  → UART RXD0 → CH340 TX (固定)
// ==================== 传感器 ====================
#define MOISTURE_SENSOR_POWER_PIN   25   // GPIO25 → 土壤湿度传感器 VCC 控制
#define MOISTURE_SENSOR_ADC_PIN     35   // GPIO35 → 土壤湿度模拟输入 (ADC1_CH7)
#define FLOAT_SWITCH_PIN            34   // GPIO34 → 浮球开关 (仅输入, 需外部上拉)
#define SHT30_SDA_PIN               21   // GPIO21 → SHT30 SDA
#define SHT30_SCL_PIN               22   // GPIO22 → SHT30 SCL
#define DS18B20_BUS_PIN             16   // GPIO16 → DS18B20 单总线 (两个传感器)
#define DS18B20_MAX_COUNT           2
// ==================== 温度保护 ====================
#define OVERTEMP_DETECT_PIN         36   // GPIO36 → 过温检测信号 (仅输入, ADC1_CH0)
// ==================== 执行器 - NMOS 控制 ====================
#define IRRIGATION_CONTROL_PIN      12   // GPIO12 → 蠕动泵 (strapping, 上电天然低→泵不转)
#define VENTILATION_CONTROL_PIN     13   // GPIO13 → 通风风扇
#define COB_FAN_CONTROL_PIN         4    // GPIO4  → COB 散热风扇
#define TEC_HOT_FAN_CONTROL_PIN     17   // GPIO17 → TEC 热端散热风扇
#define TEC_COLD_FAN_CONTROL_PIN    23   // GPIO23 → TEC 冷端散热风扇
// ==================== 执行器 - PWM ====================
#define LIGHT_PWM_PIN               14   // GPIO14 → 升压恒流光照 PWM (LEDC)
// ==================== TEC H 桥 ====================
#define TEC_PWM_H_PIN               18   // GPIO18 → IR2104 半桥 H (MCPWM)
#define TEC_PWM_L_PIN               19   // GPIO19 → IR2104 半桥 L (MCPWM)

void app_main(void) {
    ESP_LOGI("MAIN", "System starting, initializing components...");
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI("MAIN", "Initializing GPIO Control..."); // If reported error here, check gpio_control.c for pin configuration
    gpio_control_init();

    ESP_LOGI("MAIN", "Initializing Time Manager...");
    time_manager_init();    

    // 在 time_manager_init() 之后记录启动时间
    //start_time = time_manager_get_unix_time();

    ESP_LOGI("MAIN", "Initializing Input Parser...");
    input_parser_init(false, INPUT_MODE_NON_BLOCKING);

    ESP_LOGI("MAIN", "Initializing Command Processor...");
    command_processor_init();

    ESP_LOGI("MAIN", "Initializing Moisture Sensor...");
    moisture_sensor_init(MOISTURE_SENSOR_POWER_PIN, MOISTURE_SENSOR_ADC_PIN); 
    
    ESP_LOGI("MAIN", "Initializing DS18B20 Sensor...");
    ds18b20_sensor_init(DS18B20_BUS_PIN, DS18B20_MAX_COUNT);
    ds18b20_sensor_start_continuous();

    ESP_LOGI("MAIN", "Initializing SHT30 Sensor...");
    sht30_sensor_init(SHT30_SDA_PIN, SHT30_SCL_PIN);
    sht30_sensor_start();

    ESP_LOGI("MAIN", "Initializing Float Switch...");
    float_switch_init(FLOAT_SWITCH_PIN);
    float_switch_start_monitor(500);

    ESP_LOGI("MAIN", "Initializing Ventilation Control...");
    ventilation_control_init(VENTILATION_CONTROL_PIN);
    ventilation_control_start();

    ESP_LOGI("MAIN", "Initializing Light Control...");
    light_control_init((const int[]){LIGHT_PWM_PIN1, LIGHT_PWM_PIN2, LIGHT_PWM_PIN3, LIGHT_PWM_PIN4}, LIGHT_FAN_PIN);

    ESP_LOGI("MAIN", "Initializing Irrigation Controller...");
    irrigation_controller_init(IRRIGATION_CONTROL_PIN);

    ESP_LOGI("MAIN", "Initializing Cloud Communication...");
    cloud_comm_init();
    cloud_comm_start();

    ESP_LOGI("MAIN", "All components initialized.");

    while (1) {
        // 轮询输入
        input_parser_poll();
        light_control_poll();
        irrigation_controller_poll();

        if (input_parser_frame_ready()) {
            const char* frame = input_parser_get_frame();
            if (frame != NULL) {
                command_processor_process_frame(frame);
            }
        }
        // 主循环延时
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
