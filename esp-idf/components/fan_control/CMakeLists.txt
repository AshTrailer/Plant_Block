idf_component_register(
    SRCS "fan_control.c"
    INCLUDE_DIRS "."
    REQUIRES 
        driver
        pin_definitions
)