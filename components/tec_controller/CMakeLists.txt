idf_component_register(
    SRCS "tec_controller.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        nvs_flash
        freertos
        fan_control
        sht30_sensor
        ds18b20_sensor
        data_processor
        ventilation_control
        event_bus
)