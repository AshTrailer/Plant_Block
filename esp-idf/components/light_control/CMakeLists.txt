idf_component_register(
    SRCS "light_control.c"
    INCLUDE_DIRS "."
    REQUIRES 
        gpio_control
        time_manager
        cloud_comm
)