/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "app_config.h"
#include "cli_console.h"
#include "cli_console_internal.h"

/* Bien nay do main.c dat 1 lan luc boot (xem app_current_mode_set() trong
 * main.c) - cho phep 'mode_show' phan biet ro "mode DA LUU trong NVS" (se ap
 * dung sau khi reboot) voi "mode DANG CHAY that su trong phien nay" (co the
 * khac nhau ngay sau khi vua 'mode_set' nhung chua reboot). */
static app_mode_t s_running_mode = APP_MODE_WIFI_MQTT;
static bool s_running_mode_known = false;

void cli_console_set_running_mode(app_mode_t mode)
{
    s_running_mode = mode;
    s_running_mode_known = true;
}

static struct {
    struct arg_str *mode;
    struct arg_end *end;
} s_mode_set_args;

static int cmd_mode_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_mode_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_mode_set_args.end, argv[0]);
        return 1;
    }

    const char *mode_str = s_mode_set_args.mode->sval[0];
    app_mode_t mode;
    if (strcmp(mode_str, "wifi_mqtt") == 0) {
        mode = APP_MODE_WIFI_MQTT;
    } else if (strcmp(mode_str, "lorawan") == 0) {
        mode = APP_MODE_LORAWAN;
    } else {
        printf("Loi: mode phai la 'wifi_mqtt' hoac 'lorawan'\n");
        return 1;
    }

    app_config_t cfg;
    app_config_load(&cfg);
    cfg.mode = mode;

    esp_err_t err = app_config_save(&cfg);
    if (err == ESP_OK) {
        printf("OK. Da luu mode = %s. CAN REBOOT de ap dung - chi 1 stack (WiFi/MQTT HOAC LoRaWAN) duoc khoi tao moi lan boot.\n", mode_str);
    } else {
        printf("Loi luu cau hinh: %s\n", esp_err_to_name(err));
    }
    return err == ESP_OK ? 0 : 1;
}

static int cmd_mode_show(int argc, char **argv)
{
    app_config_t cfg;
    app_config_load(&cfg);
    printf("mode da luu (NVS)   = %s\n", cfg.mode == APP_MODE_WIFI_MQTT ? "wifi_mqtt" : "lorawan");
    if (s_running_mode_known) {
        printf("mode dang chay (RAM) = %s%s\n",
               s_running_mode == APP_MODE_WIFI_MQTT ? "wifi_mqtt" : "lorawan",
               cfg.mode != s_running_mode ? "  <-- KHAC voi mode da luu, reboot de ap dung mode moi" : "");
    }
    return 0;
}

void register_mode_cmds(void)
{
    s_mode_set_args.mode = arg_str1(NULL, NULL, "<wifi_mqtt|lorawan>", "Giao thuc ket noi server");
    s_mode_set_args.end  = arg_end(1);

    const esp_console_cmd_t mode_set_cmd = {
        .command = "mode_set",
        .help = "Chon mode ket noi (wifi_mqtt hoac lorawan). Can reboot de ap dung.",
        .hint = NULL,
        .func = &cmd_mode_set,
        .argtable = &s_mode_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mode_set_cmd));

    const esp_console_cmd_t mode_show_cmd = {
        .command = "mode_show",
        .help = "In mode da luu (NVS) va mode dang chay thuc te (co the khac nhau truoc khi reboot)",
        .hint = NULL,
        .func = &cmd_mode_show,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mode_show_cmd));
}
