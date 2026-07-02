idf_component_register(
    SRCS "ds18b20_sensor.c"
    INCLUDE_DIRS "."
    REQUIRES 
        cloud_comm
        espressif__onewire_bus 
        espressif__ds18b20
)