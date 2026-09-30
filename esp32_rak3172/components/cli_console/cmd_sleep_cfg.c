/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "app_config.h"
#include "cli_console_internal.h"

static struct {
    struct arg_str *state;         /* bat buoc: on|off */
    struct arg_int *interval_ms;   /* tuy chon - chi dung khi state=on, giu nguyen gia tri cu neu bo qua */
    struct arg_end *end;
} s_sleep_args;

static int cmd_sleep_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_sleep_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_sleep_args.end, argv[0]);
        return 1;
    }

    const char *state_str = s_sleep_args.state->sval[0];
    bool enable;
    if (strcmp(state_str, "on") == 0) {
        enable = true;
    } else if (strcmp(state_str, "off") == 0) {
        enable = false;
    } else {
        printf("Loi: <on|off> phai la 'on' hoac 'off'\n");
        return 1;
    }

    app_config_t cfg;
    app_config_load(&cfg);

    cfg.sleep_enabled = enable;
    if (s_sleep_args.interval_ms->count > 0) {
        int val = s_sleep_args.interval_ms->ival[0];
        if (val <= 0) {
            printf("Loi: <interval_ms> phai > 0\n");
            return 1;
        }
        cfg.sleep_interval_ms = (uint32_t)val;
    }

    esp_err_t err = app_config_save(&cfg);
    if (err == ESP_OK) {
        printf("OK. Can 'reboot' de ap dung. sleep_enabled=%s sleep_interval_ms=%lu\n",
               cfg.sleep_enabled ? "on" : "off", (unsigned long)cfg.sleep_interval_ms);
        if (enable) {
            printf("Luu y: khi da reboot vao sleep mode, ESP32 Deep Sleep giua cac chu ky - CLI console\n"
                   "chi truy cap duoc trong khoang thoi gian ngan moi lan thuc day. Neu can tat sleep lai,\n"
                   "go 'sleep_set off' ngay khi thay log vua thuc, roi 'reboot'.\n");
        }
    } else {
        printf("Loi luu cau hinh: %s\n", esp_err_to_name(err));
    }
    return err == ESP_OK ? 0 : 1;
}

static int cmd_sleep_show(int argc, char **argv)
{
    app_config_t cfg;
    app_config_load(&cfg);

    printf("sleep_enabled     = %s\n", cfg.sleep_enabled ? "on" : "off");
    printf("sleep_interval_ms = %lu\n", (unsigned long)cfg.sleep_interval_ms);
    return 0;
}

void register_sleep_cfg_cmds(void)
{
    s_sleep_args.state       = arg_str1(NULL, NULL, "<on|off>", "Bat/tat Deep Sleep giua cac chu ky publish");
    s_sleep_args.interval_ms = arg_int0(NULL, NULL, "<interval_ms>", "Chu ky thuc-ngu (ms), bo qua = giu nguyen gia tri cu");
    s_sleep_args.end         = arg_end(2);

    const esp_console_cmd_t sleep_set_cmd = {
        .command = "sleep_set",
        .help = "sleep_set <on|off> [<interval_ms>] - Bat/tat Deep Sleep va chu ky thuc-ngu. Can reboot de ap dung.",
        .hint = NULL,
        .func = &cmd_sleep_set,
        .argtable = &s_sleep_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&sleep_set_cmd));

    const esp_console_cmd_t sleep_show_cmd = {
        .command = "sleep_show",
        .help = "In cau hinh sleep mode da luu",
        .hint = NULL,
        .func = &cmd_sleep_show,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&sleep_show_cmd));
}
