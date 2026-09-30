idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_netif esp_event nvs_flash mqtt ota_manager wifi_manager esp_app_format app_update esp_partition)
