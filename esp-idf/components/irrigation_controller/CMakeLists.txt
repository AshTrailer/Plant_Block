idf_component_register(
    SRCS "irrigation_controller.c"
    INCLUDE_DIRS "."
    REQUIRES 
        gpio_control
        time_manager
        moisture_sensor
)