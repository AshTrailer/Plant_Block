idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        input_parser
        command_processor
        gpio_control
        ventilation_control
        time_manager
        light_control
        irrigation_controller
        moisture_sensor
        cloud_comm
        ds18b20_sensor
        sht30_sensor
        float_switch
        pin_definitions
        system_monitor
        fan_control
        vofa_output
        tec_controller
)