idf_component_register(
    SRCS "cloud_comm.c"
    INCLUDE_DIRS "."
    REQUIRES
        mqtt
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp-tls
        time_manager
        command_processor
)