/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"

#include "app_config.h"
#include "cli_console_internal.h"

static struct {
    struct arg_str *ssid;
    struct arg_str *pass;
    struct arg_end *end;
} s_wifi_set_args;

static int cmd_wifi_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_wifi_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_wifi_set_args.end, argv[0]);
        return 1;
    }

    const char *ssid = s_wifi_set_args.ssid->sval[0];
    const char *pass = s_wifi_set_args.pass->sval[0];
    if (strlen(ssid) >= APP_CONFIG_SSID_MAXLEN) {
        printf("Loi: SSID qua dai (toi da %d ky tu)\n", APP_CONFIG_SSID_MAXLEN - 1);
        return 1;
    }
    if (strlen(pass) >= APP_CONFIG_WIFI_PASS_MAXLEN) {
        printf("Loi: WiFi password qua dai (toi da %d ky tu)\n", APP_CONFIG_WIFI_PASS_MAXLEN - 1);
        return 1;
    }

    app_config_t cfg;
    app_config_load(&cfg);   /* tolerate ESP_ERR_NVS_NOT_FOUND - cfg da duoc dien default */
    strlcpy(cfg.wifi_ssid, ssid, sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_pass, pass, sizeof(cfg.wifi_pass));

    esp_err_t err = app_config_save(&cfg);
    if (err == ESP_OK) {
        printf("OK. Can 'mode_set wifi_mqtt' (neu chua) va 'reboot' de ap dung.\n");
    } else {
        printf("Loi luu cau hinh: %s\n", esp_err_to_name(err));
    }
    return err == ESP_OK ? 0 : 1;
}

static int cmd_wifi_show(int argc, char **argv)
{
    app_config_t cfg;
    app_config_load(&cfg);
    printf("wifi_ssid = %s\n", cfg.wifi_ssid);
    printf("wifi_pass = %s\n", strlen(cfg.wifi_pass) > 0 ? "********" : "(chua dat)");
    return 0;
}

void register_wifi_cfg_cmds(void)
{
    s_wifi_set_args.ssid = arg_str1(NULL, NULL, "<ssid>", "Ten mang WiFi (SSID)");
    s_wifi_set_args.pass = arg_str1(NULL, NULL, "<password>", "Mat khau WiFi");
    s_wifi_set_args.end  = arg_end(2);

    const esp_console_cmd_t wifi_set_cmd = {
        .command = "wifi_set",
        .help = "Luu SSID/password WiFi vao NVS. Can reboot de ap dung.",
        .hint = NULL,
        .func = &cmd_wifi_set,
        .argtable = &s_wifi_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_set_cmd));

    const esp_console_cmd_t wifi_show_cmd = {
        .command = "wifi_show",
        .help = "In cau hinh WiFi da luu (password bi che)",
        .hint = NULL,
        .func = &cmd_wifi_show,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_show_cmd));
}
