idf_component_register(
    SRCS "ota_manager.c" "ota_rollback.c" "ota_task.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt
    PRIV_REQUIRES esp_https_ota esp_http_client json app_update esp_event esp_timer
    EMBED_TXTFILES "server_certs/ca_cert.pem"
)
