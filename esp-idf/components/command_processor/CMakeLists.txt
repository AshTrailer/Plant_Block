idf_component_register(
    SRCS "command_processor.c" 
    INCLUDE_DIRS "."
    REQUIRES 
        light_control
        time_manager
)