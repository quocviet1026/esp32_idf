/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "app_config.h"
#include "rak3172.h"
#include "cli_console_internal.h"

static struct {
    struct arg_str *deveui;
    struct arg_str *appeui;
    struct arg_str *appkey;
    struct arg_end *end;
} s_lorawan_set_args;

/* Kiem tra chuoi chi gom ky tu hex va dung do dai (khong tinh NUL). */
static bool is_hex_string(const char *s, size_t expected_len)
{
    size_t len = strlen(s);
    if (len != expected_len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

static void to_upper_copy(char *dst, const char *src, size_t dst_cap)
{
    size_t i;
    for (i = 0; i + 1 < dst_cap && src[i] != '\0'; i++) {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static int cmd_lorawan_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_lorawan_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_lorawan_set_args.end, argv[0]);
        return 1;
    }

    const char *deveui = s_lorawan_set_args.deveui->sval[0];
    const char *appeui = s_lorawan_set_args.appeui->sval[0];
    const char *appkey = s_lorawan_set_args.appkey->sval[0];

    if (!is_hex_string(deveui, 16)) {
        printf("Loi: DevEUI phai la 16 ky tu hex (8 byte)\n");
        return 1;
    }
    if (!is_hex_string(appeui, 16)) {
        printf("Loi: AppEUI phai la 16 ky tu hex (8 byte)\n");
        return 1;
    }
    if (!is_hex_string(appkey, 32)) {
        printf("Loi: AppKey phai la 32 ky tu hex (16 byte)\n");
        return 1;
    }

    app_config_t cfg;
    app_config_load(&cfg);
    to_upper_copy(cfg.lorawan_deveui, deveui, sizeof(cfg.lorawan_deveui));
    to_upper_copy(cfg.lorawan_appeui, appeui, sizeof(cfg.lorawan_appeui));
    to_upper_copy(cfg.lorawan_appkey, appkey, sizeof(cfg.lorawan_appkey));

    esp_err_t err = app_config_save(&cfg);
    if (err == ESP_OK) {
        printf("OK. Can 'mode_set lorawan' (neu chua) va 'reboot' de ap dung.\n");
    } else {
        printf("Loi luu cau hinh: %s\n", esp_err_to_name(err));
    }
    return err == ESP_OK ? 0 : 1;
}

static int cmd_lorawan_show(int argc, char **argv)
{
    app_config_t cfg;
    app_config_load(&cfg);
    printf("lorawan_deveui = %s\n", cfg.lorawan_deveui);
    printf("lorawan_appeui = %s\n", cfg.lorawan_appeui);
    printf("lorawan_appkey = %s\n", strlen(cfg.lorawan_appkey) > 0 ? "********************************" : "(chua dat)");
    return 0;
}

static int cmd_lorawan_join(int argc, char **argv)
{
    esp_err_t err = rak3172_join();
    if (err == ESP_ERR_INVALID_STATE) {
        printf("Loi: rak3172 chua duoc khoi tao - thiet bi co dang chay o mode LORAWAN khong? (xem 'mode_show')\n");
        return 1;
    }
    if (err != ESP_OK) {
        printf("Gui lenh AT+JOIN that bai: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Da bat dau qua trinh join - theo doi ket qua bang 'lorawan_status' hoac log.\n");
    return 0;
}

static const char *join_state_str(rak3172_join_state_t s)
{
    switch (s) {
    case RAK3172_JOIN_IDLE:    return "IDLE (chua join lan nao)";
    case RAK3172_JOIN_JOINING: return "JOINING";
    case RAK3172_JOIN_JOINED:  return "JOINED";
    case RAK3172_JOIN_FAILED:  return "FAILED";
    default:                   return "UNKNOWN";
    }
}

static int cmd_lorawan_status(int argc, char **argv)
{
    printf("join_state = %s\n", join_state_str(rak3172_get_join_state()));

    rak3172_downlink_info_t info;
    rak3172_get_last_downlink_info(&info);
    if (info.valid) {
        printf("last_downlink: port=%u rssi=%d snr=%d\n", info.port, info.rssi, info.snr);
    } else {
        printf("last_downlink: (chua nhan downlink nao)\n");
    }
    return 0;
}

void register_lorawan_cfg_cmds(void)
{
    s_lorawan_set_args.deveui = arg_str1(NULL, NULL, "<deveui>", "DevEUI (16 ky tu hex)");
    s_lorawan_set_args.appeui = arg_str1(NULL, NULL, "<appeui>", "AppEUI/JoinEUI (16 ky tu hex)");
    s_lorawan_set_args.appkey = arg_str1(NULL, NULL, "<appkey>", "AppKey (32 ky tu hex)");
    s_lorawan_set_args.end    = arg_end(2);

    const esp_console_cmd_t lorawan_set_cmd = {
        .command = "lorawan_set",
        .help = "Luu DevEUI/AppEUI/AppKey (OTAA) vao NVS. Can reboot de ap dung.",
        .hint = NULL,
        .func = &cmd_lorawan_set,
        .argtable = &s_lorawan_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&lorawan_set_cmd));

    const esp_console_cmd_t lorawan_show_cmd = {
        .command = "lorawan_show",
        .help = "In DevEUI/AppEUI da luu (AppKey bi che)",
        .hint = NULL,
        .func = &cmd_lorawan_show,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&lorawan_show_cmd));

    const esp_console_cmd_t lorawan_join_cmd = {
        .command = "lorawan_join",
        .help = "Trigger lai qua trinh join LoRaWAN ngay (khong can reboot) - de retry sau khi join lan dau that bai",
        .hint = NULL,
        .func = &cmd_lorawan_join,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&lorawan_join_cmd));

    const esp_console_cmd_t lorawan_status_cmd = {
        .command = "lorawan_status",
        .help = "In trang thai join hien tai va downlink gan nhat (neu co)",
        .hint = NULL,
        .func = &cmd_lorawan_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&lorawan_status_cmd));
}
