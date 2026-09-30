idf_component_register(SRCS "main.c" "sensor_sim.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_hw_support esp_partition app_update
                             app_config cli_console transport esp_driver_usb_serial_jtag ota_manager)
