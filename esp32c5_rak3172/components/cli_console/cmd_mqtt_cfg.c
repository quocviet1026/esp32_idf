/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "app_config.h"
#include "cli_console_internal.h"

static struct {
    struct arg_str *host;
    struct arg_int *port;
    struct arg_str *user;
    struct arg_str *pass;
    struct arg_end *end;
} s_mqtt_set_args;

static int cmd_mqtt_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_mqtt_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_mqtt_set_args.end, argv[0]);
        return 1;
    }

    const char *host = s_mqtt_set_args.host->sval[0];
    int port = s_mqtt_set_args.port->ival[0];
    const char *user = s_mqtt_set_args.user->sval[0];
    const char *pass = s_mqtt_set_args.pass->sval[0];

    if (strlen(host) >= APP_CONFIG_MQTT_HOST_MAXLEN) {
        printf("Loi: host qua dai (toi da %d ky tu)\n", APP_CONFIG_MQTT_HOST_MAXLEN - 1);
        return 1;
    }
    if (port < 1 || port > 65535) {
        printf("Loi: port phai trong khoang 1-65535\n");
        return 1;
    }
    if (strlen(user) >= APP_CONFIG_MQTT_USER_MAXLEN || strlen(pass) >= APP_CONFIG_MQTT_PASS_MAXLEN) {
        printf("Loi: username/password qua dai\n");
        return 1;
    }

    app_config_t cfg;
    app_config_load(&cfg);
    strlcpy(cfg.mqtt_host, host, sizeof(cfg.mqtt_host));
    cfg.mqtt_port = (uint16_t)port;
    strlcpy(cfg.mqtt_user, user, sizeof(cfg.mqtt_user));
    strlcpy(cfg.mqtt_pass, pass, sizeof(cfg.mqtt_pass));

    esp_err_t err = app_config_save(&cfg);
    if (err == ESP_OK) {
        printf("OK. Can 'mode_set wifi_mqtt' (neu chua) va 'reboot' de ap dung.\n");
    } else {
        printf("Loi luu cau hinh: %s\n", esp_err_to_name(err));
    }
    return err == ESP_OK ? 0 : 1;
}

static int cmd_mqtt_show(int argc, char **argv)
{
    app_config_t cfg;
    app_config_load(&cfg);
    printf("mqtt_host = %s\n", cfg.mqtt_host);
    printf("mqtt_port = %u\n", cfg.mqtt_port);
    printf("mqtt_user = %s\n", cfg.mqtt_user);
    printf("mqtt_pass = %s\n", strlen(cfg.mqtt_pass) > 0 ? "********" : "(chua dat)");
    return 0;
}

void register_mqtt_cfg_cmds(void)
{
    s_mqtt_set_args.host = arg_str1(NULL, NULL, "<host>", "IP hoac hostname cua MQTT broker");
    s_mqtt_set_args.port = arg_int1(NULL, NULL, "<port>", "Port cua MQTT broker (1-65535)");
    s_mqtt_set_args.user = arg_str1(NULL, NULL, "<user>", "Username MQTT (chuoi rong \"\" neu broker anonymous)");
    s_mqtt_set_args.pass = arg_str1(NULL, NULL, "<pass>", "Password MQTT (chuoi rong \"\" neu broker anonymous)");
    s_mqtt_set_args.end  = arg_end(2);

    const esp_console_cmd_t mqtt_set_cmd = {
        .command = "mqtt_set",
        .help = "Luu dia chi/dang nhap MQTT broker vao NVS. Can reboot de ap dung.",
        .hint = NULL,
        .func = &cmd_mqtt_set,
        .argtable = &s_mqtt_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mqtt_set_cmd));

    const esp_console_cmd_t mqtt_show_cmd = {
        .command = "mqtt_show",
        .help = "In cau hinh MQTT da luu (password bi che)",
        .hint = NULL,
        .func = &cmd_mqtt_show,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mqtt_show_cmd));
}
