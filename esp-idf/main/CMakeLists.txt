idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        input_parser
        command_processor
        gpio_control
        ventilation_control
)