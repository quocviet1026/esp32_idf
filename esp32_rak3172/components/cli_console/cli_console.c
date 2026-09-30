/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* cli_console.c
 *
 * BAN CLONE cho phan cung ESP32 Xtensa that (giong board /home/vietnq/esp/ota) -
 * dung esp_console_new_repl_uart() + esp_console_start_repl() tren UART0,
 * KHAC ban goc esp32c5_rak3172 (dung esp_console_new_repl_usb_serial_jtag()
 * vi ESP32-C5 co USB native). ESP32 goc KHONG co phan cung USB-Serial-JTAG -
 * dung API do se khong compile duoc (bi #if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
 * chan trong esp_console.h). UART0 tren board ESP32 thuong noi qua 1 chip cau
 * UART-USB rieng (CP2102/CH340...) ra dung cong USB nap code - giong het cach
 * esp/ota dang hoat dong. Khong lien quan gi toi UART rieng cua module
 * RAK3172 (xem components/rak3172, dung 1 UART vat ly khac hoan toan).
 */

#include "esp_console.h"
#include "esp_log.h"

#include "cli_console.h"
#include "cli_console_internal.h"

static const char *TAG = "cli_console";

void cli_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp32> ";
    repl_config.max_cmdline_length = 256;

    esp_console_register_help_command();
    register_wifi_cfg_cmds();
    register_mqtt_cfg_cmds();
    register_lorawan_cfg_cmds();
    register_mode_cmds();
    register_system_cmds();
    register_net_cfg_cmds();
    register_sleep_cfg_cmds();

    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    ESP_LOGI(TAG, "CLI console san sang tren UART console mac dinh - go 'help' de xem danh sach lenh");
}
