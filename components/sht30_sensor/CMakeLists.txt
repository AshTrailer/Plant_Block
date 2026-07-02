idf_component_register(
    SRCS "sht30_sensor.c"
    INCLUDE_DIRS "."
    REQUIRES 
        sht3x
        cloud_comm
)