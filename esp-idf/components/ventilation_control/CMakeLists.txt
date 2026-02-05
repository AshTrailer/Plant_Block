idf_component_register(
    SRCS "ventilation_control.c"
    INCLUDE_DIRS "."
    REQUIRES 
        gpio_control
)