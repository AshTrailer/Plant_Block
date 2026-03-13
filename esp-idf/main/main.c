#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

#define HMI_UART_TX_PIN 16
#define HMI_UART_RX_PIN 17
#define MOISTURE_SENSOR_POWER_PIN 15
#define MOISTURE_SENSOR_ADC_PIN 32
#define VENTILATION_CONTROL_PIN 5
#define IRRIGATION_CONTROL_PIN 18
#define LIGHT_POWER_PIN 12
#define LIGHT_PWM_PIN 14

void app_main(void) {
    ESP_LOGI("MAIN", "System starting, initializing components...");
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI("MAIN", "Initializing GPIO Control..."); // If reported error here, check gpio_control.c for pin configuration
    gpio_control_init();

    ESP_LOGI("MAIN", "Initializing HMI UART...");
    hmi_uart_init(HMI_UART_TX_PIN, HMI_UART_RX_PIN); 

    ESP_LOGI("MAIN", "Initializing Time Manager...");
    time_manager_init();    

    ESP_LOGI("MAIN", "Initializing Input Parser...");
    input_parser_init(false, INPUT_MODE_NON_BLOCKING);

    ESP_LOGI("MAIN", "Initializing Command Processor...");
    command_processor_init();

    ESP_LOGI("MAIN", "Initializing Moisture Sensor...");
    moisture_sensor_init(MOISTURE_SENSOR_POWER_PIN, MOISTURE_SENSOR_ADC_PIN); 

    ESP_LOGI("MAIN", "Initializing Ventilation Control...");
    ventilation_control_init(VENTILATION_CONTROL_PIN);
    ventilation_control_start();

    ESP_LOGI("MAIN", "Initializing Light Control...");
    light_control_init(LIGHT_POWER_PIN, LIGHT_PWM_PIN);

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

        hmi_uart_poll();

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