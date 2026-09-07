idf_component_register(
    SRCS "float_switch.c"
    INCLUDE_DIRS "."
    REQUIRES 
        event_bus
        driver
)