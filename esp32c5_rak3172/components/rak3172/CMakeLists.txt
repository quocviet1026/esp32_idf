idf_component_register(
    SRCS "rak3172.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_uart esp_driver_gpio freertos esp_timer
)
