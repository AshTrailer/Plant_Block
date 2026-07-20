idf_component_register(
    SRCS "command_processor.c" 
    INCLUDE_DIRS "."
    REQUIRES 
        light_control
        time_manager
        irrigation_controller
        moisture_sensor
        cloud_comm
        system_monitor
        sht30_sensor
        ds18b20_sensor
        float_switch
)