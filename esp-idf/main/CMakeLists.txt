idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        startup_logger
        input_parser
        command_processor
        gpio_control
)