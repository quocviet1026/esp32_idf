/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* cli_console.c
 *
 * Dung esp_console_new_repl_usb_serial_jtag() + esp_console_start_repl() -
 * wrapper cua ESP-IDF tu quan ly toan bo 1 REPL task rieng, chay tren cong
 * USB NATIVE cua ESP32-C5 (USB-Serial-JTAG - chip C5 co san USB PHY, khong
 * can chip cau UART-USB rieng nhu ESP32 goc). Board C5 thuong chi co dung 1
 * cong USB duy nhat de vua nap code vua xem log/go lenh, nen chon kenh nay
 * thay vi UART0 - khong lien quan gi toi UART rieng cua module RAK3172 (xem
 * components/rak3172, dung 1 UART vat ly khac hoan toan).
 *
 * De kenh nay hoat dong, phai bat dung Kconfig:
 *   Component config -> ESP System Settings -> Channel for console output
 *   -> USB Serial/JTAG Controller
 * (da dat san trong sdkconfig.defaults: CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y)
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
    repl_config.prompt = "esp32c5> ";
    repl_config.max_cmdline_length = 256;

    esp_console_register_help_command();
    register_wifi_cfg_cmds();
    register_mqtt_cfg_cmds();
    register_lorawan_cfg_cmds();
    register_mode_cmds();
    register_system_cmds();
    register_net_cfg_cmds();
    register_sleep_cfg_cmds();

    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    ESP_LOGI(TAG, "CLI console san sang tren cong USB native (USB-Serial-JTAG) - go 'help' de xem danh sach lenh");
}
