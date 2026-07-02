idf_component_register(
    SRCS "moisture_sensor.c"
    INCLUDE_DIRS "."
    REQUIRES 
        gpio_control 
        driver
        esp_adc
        data_processor
        cloud_comm
)