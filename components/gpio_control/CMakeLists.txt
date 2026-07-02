idf_component_register(
    SRCS "gpio_control.c"
    INCLUDE_DIRS "."
    REQUIRES 
        driver
        ventilation_control
        cloud_comm
)