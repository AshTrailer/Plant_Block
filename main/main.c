#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "tec_controller.h"
#include "pin_definitions.h"
#include "time_manager.h"
#include "input_parser.h"
#include "command_processor.h"
#include "gpio_control.h"
#include "ventilation_control.h"
#include "light_control.h"
#include "irrigation_controller.h"
#include "moisture_sensor.h"
#include "cloud_comm.h"
#include "ds18b20_sensor.h"
#include "sht30_sensor.h"
#include "float_switch.h"
#include "fan_control.h"
#include "vofa_output.h"
#include "event_bus.h"
#include "system_monitor.h"

static const char *TAG = "MAIN";

static TaskHandle_t s_vofa_task_handle = NULL; 

// ==================== Vofa+ 传感器数据发布任务 ====================
static void vofa_sensor_task(void *arg)
{
   (void)arg;
   vTaskDelay(pdMS_TO_TICKS(3000));
   ESP_LOGI(TAG, "Vofa sensor task started");
   while (1) {
      float sht30_t = 0, sht30_h = 0;
      sht30_sensor_get_data(&sht30_t, &sht30_h);
      float ds18_cold = -999, ds18_hot = -999;
      ds18b20_sensor_get_temperature_cached(0, &ds18_cold);   // ← 改用 cached
      ds18b20_sensor_get_temperature_cached(1, &ds18_hot);    // ← 改用 cached
      // 湿度传感器由 irrigation_controller 按需读取，此处不再轮询
      float moist_pct = 0.0f;
      uint32_t moist_mv = 0;
      bool has_water = float_switch_get_state();
      bool ntc_overtemp = (gpio_get_level(PIN_NTC_OVERTEMP) == 1);
      bool light_on = light_control_is_on();
      uint8_t light_duty = light_control_get_pwm_duty();
      bool pump_on = false;
      uint8_t pump_speed = 0;
      vofa_output_send_sensor_frame(sht30_t, sht30_h,
                                    ds18_cold, ds18_hot,
                                    moist_pct, moist_mv,
                                    has_water, ntc_overtemp,
                                    light_duty, light_on,
                                    pump_speed, pump_on);
      vTaskDelay(pdMS_TO_TICKS(1000));
   }
}

void app_main(void)
{
   ESP_LOGI(TAG, "========================================");
   ESP_LOGI(TAG, "  Plant Grow Controller V2.0 Boot");
   ESP_LOGI(TAG, "========================================");
   esp_log_level_set("*", ESP_LOG_INFO);

   // ---- 基础设施 ----
   ESP_LOGI(TAG, "[0/9] Initializing Event Bus...");
   event_bus_init();

   ESP_LOGI(TAG, "[1/9] Initializing GPIO Control...");
   gpio_control_init();

   ESP_LOGI(TAG, "[2/9] Initializing Vofa+ Output (UART0, 115200bps)...");
   vofa_output_init(UART_NUM_0, 115200);
   vofa_output_subscribe_all();

   ESP_LOGI(TAG, "[3/9] Initializing Time Manager...");
   time_manager_init();

   ESP_LOGI(TAG, "[4/9] Initializing Input Parser (non-blocking)...");
   input_parser_init(false, INPUT_MODE_NON_BLOCKING);

   ESP_LOGI(TAG, "[5/9] Initializing Command Processor...");
   command_processor_init();

   // ---- 传感器 ----
   ESP_LOGI(TAG, "[6/9] Initializing Sensors...");
   moisture_sensor_init(PIN_MOISTURE_POWER, PIN_MOISTURE_ADC);
   ds18b20_sensor_init(PIN_DS18B20_BUS, PIN_DS18B20_MAX_COUNT);
   ds18b20_sensor_start_continuous();
   sht30_sensor_init(PIN_SHT30_SDA, PIN_SHT30_SCL);
   sht30_sensor_start();
   float_switch_init(PIN_FLOAT_SWITCH);
   float_switch_start_monitor(500);

   // ---- 执行器 ----
   ESP_LOGI(TAG, "[7/9] Initializing Actuators...");
   fan_control_init();
   ventilation_control_init(PIN_VENTILATION_FAN);
   ventilation_control_start();

   const int light_pins[LIGHT_CHANNEL_COUNT] = { PIN_COB_LED_PWM };
   light_control_init(light_pins, PIN_COB_LED_POWER);  // 传入电源引脚

   irrigation_controller_init(PIN_IRRIGATION_PUMP);

   // ---- TEC 温度控制 ----
   ESP_LOGI(TAG, "[7.5/9] Initializing TEC Controller...");
   tec_controller_init();

   // ---- 云端通信 ----
   ESP_LOGI(TAG, "[8/9] Initializing Cloud Communication...");
   cloud_comm_init();
   cloud_comm_start();

   // ---- 系统监控 ----
   ESP_LOGI(TAG, "[9/9] Initializing System Monitor...");
   system_monitor_init(10000, 256);

   // ---- Vofa+ 数据任务 ----
   xTaskCreate(vofa_sensor_task, "vofa_sensor", 4096, NULL,
               tskIDLE_PRIORITY + 3, &s_vofa_task_handle);
   if (s_vofa_task_handle) {
      system_monitor_register_task("vofa_sensor", s_vofa_task_handle);
   }

   ESP_LOGI(TAG, "========================================");
   ESP_LOGI(TAG, "  All components initialized.");
   ESP_LOGI(TAG, "  Ready. Type 'help' for commands.");
   ESP_LOGI(TAG, "========================================");

   while (1) {
      input_parser_poll();
      light_control_poll();
      irrigation_controller_poll();

      if (input_parser_frame_ready()) {
         const char *frame = input_parser_get_frame();
         if (frame != NULL) {
            command_processor_process_frame(frame);
         }
      }

      system_monitor_feed_watchdog();
      vTaskDelay(50 / portTICK_PERIOD_MS);
   }
}