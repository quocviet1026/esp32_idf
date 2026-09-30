/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_app_desc.h"

#include "app_config.h"
#include "cli_console_internal.h"

static int cmd_config_show(int argc, char **argv)
{
    app_config_t cfg;
    app_config_load(&cfg);

    printf("mode           = %s\n", cfg.mode == APP_MODE_WIFI_MQTT ? "wifi_mqtt" : "lorawan");
    printf("wifi_ssid      = %s\n", cfg.wifi_ssid);
    printf("wifi_pass      = %s\n", strlen(cfg.wifi_pass) > 0 ? "********" : "(chua dat)");
    printf("mqtt_host      = %s\n", cfg.mqtt_host);
    printf("mqtt_port      = %u\n", cfg.mqtt_port);
    printf("mqtt_user      = %s\n", cfg.mqtt_user);
    printf("mqtt_pass      = %s\n", strlen(cfg.mqtt_pass) > 0 ? "********" : "(chua dat)");
    printf("net_mode       = %s\n", cfg.net_mode == APP_NET_DHCP ? "auto (DHCP)" : "manual (static IP)");
    if (cfg.net_mode == APP_NET_STATIC) {
        printf("net_ip         = %s\n", cfg.net_ip);
        printf("net_netmask    = %s\n", cfg.net_netmask);
        printf("net_gateway    = %s\n", cfg.net_gateway);
    }
    printf("lorawan_deveui = %s\n", cfg.lorawan_deveui);
    printf("lorawan_appeui = %s\n", cfg.lorawan_appeui);
    printf("lorawan_appkey = %s\n", strlen(cfg.lorawan_appkey) > 0 ? "********************************" : "(chua dat)");
    printf("sleep_enabled     = %s\n", cfg.sleep_enabled ? "on" : "off");
    printf("sleep_interval_ms = %lu\n", (unsigned long)cfg.sleep_interval_ms);
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    printf("Rebooting...\n");
    fflush(stdout);
    esp_restart();
    return 0;   /* khong bao gio toi day */
}

static int cmd_version(int argc, char **argv)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    printf("Firmware version = %s\n", app_desc->version);
    printf("ESP-IDF version  = %s\n", esp_get_idf_version());
    return 0;
}

void register_system_cmds(void)
{
    const esp_console_cmd_t config_show_cmd = {
        .command = "config_show",
        .help = "In toan bo cau hinh da luu trong NVS (password/appkey bi che)",
        .hint = NULL,
        .func = &cmd_config_show,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&config_show_cmd));

    const esp_console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Khoi dong lai thiet bi (can sau moi lenh *_set/mode_set de ap dung)",
        .hint = NULL,
        .func = &cmd_reboot,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd));

    const esp_console_cmd_t version_cmd = {
        .command = "version",
        .help = "In phien ban firmware va ESP-IDF",
        .hint = NULL,
        .func = &cmd_version,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&version_cmd));
}
