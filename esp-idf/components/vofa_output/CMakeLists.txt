idf_component_register(
    SRCS "vofa_output.c"
    INCLUDE_DIRS "."
    REQUIRES 
        event_bus
        driver
        cloud_comm
)