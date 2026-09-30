idf_component_register(
    SRCS "cli_console.c" "cmd_wifi_cfg.c" "cmd_mqtt_cfg.c" "cmd_lorawan_cfg.c" "cmd_mode.c" "cmd_system.c" "cmd_net_cfg.c" "cmd_sleep_cfg.c"
    INCLUDE_DIRS "include"
    REQUIRES app_config
    PRIV_REQUIRES console rak3172 esp_system app_update esp_netif
)
