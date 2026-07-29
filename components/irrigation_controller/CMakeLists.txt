idf_component_register(
    SRCS "irrigation_controller.c"
    INCLUDE_DIRS "."
    REQUIRES 
        gpio_control
        time_manager
        moisture_sensor
        cloud_comm
        float_switch
        vofa_output
)