idf_component_register(
    SRCS "transport.c" "transport_wifi_mqtt.c" "transport_lorawan.c"
    INCLUDE_DIRS "include"
    REQUIRES app_config
    PRIV_REQUIRES wifi_manager ota_manager rak3172 mqtt esp_event esp_netif esp_app_format
)
