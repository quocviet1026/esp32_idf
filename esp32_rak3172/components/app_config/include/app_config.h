/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_MAGIC     0x41435F31u   /* "AC_1" */
#define APP_CONFIG_VERSION   3u            /* bump: them sleep_enabled/sleep_interval_ms (v2 -> config cu tu dong bi coi la NOT_FOUND, ve default an toan) */

#define APP_CONFIG_SSID_MAXLEN      32     /* matches wifi_config_t.sta.ssid */
#define APP_CONFIG_WIFI_PASS_MAXLEN 64     /* matches wifi_config_t.sta.password */
#define APP_CONFIG_MQTT_HOST_MAXLEN 128
#define APP_CONFIG_MQTT_USER_MAXLEN 64
#define APP_CONFIG_MQTT_PASS_MAXLEN 64
#define APP_CONFIG_HEX8_LEN         17     /* 8 byte hex (DevEUI/AppEUI) + NUL */
#define APP_CONFIG_HEX16_LEN        33     /* 16 byte hex (AppKey) + NUL */
#define APP_CONFIG_IPV4_MAXLEN      16     /* "255.255.255.255" + NUL */

typedef enum {
    APP_MODE_WIFI_MQTT = 0,
    APP_MODE_LORAWAN   = 1,
} app_mode_t;

typedef enum {
    APP_NET_DHCP   = 0,   /* mac dinh - giu hanh vi hien tai, khong doi gi */
    APP_NET_STATIC = 1,
} app_net_mode_t;

typedef struct {
    uint32_t   magic;
    uint32_t   version;

    app_mode_t mode;

    char       wifi_ssid[APP_CONFIG_SSID_MAXLEN];
    char       wifi_pass[APP_CONFIG_WIFI_PASS_MAXLEN];

    char       mqtt_host[APP_CONFIG_MQTT_HOST_MAXLEN];
    uint16_t   mqtt_port;
    char       mqtt_user[APP_CONFIG_MQTT_USER_MAXLEN];
    char       mqtt_pass[APP_CONFIG_MQTT_PASS_MAXLEN];

    /* Cau hinh IP tinh cho WiFi STA - chi co y nghia khi mode == APP_MODE_WIFI_MQTT.
     * net_mode == APP_NET_DHCP thi cac truong net_ip/netmask/gateway/dns bi
     * bo qua (giu hanh vi DHCP hien tai). dns1/dns2 rong ("") nghia la khong dat. */
    app_net_mode_t net_mode;
    char       net_ip[APP_CONFIG_IPV4_MAXLEN];
    char       net_netmask[APP_CONFIG_IPV4_MAXLEN];
    char       net_gateway[APP_CONFIG_IPV4_MAXLEN];
    char       net_dns1[APP_CONFIG_IPV4_MAXLEN];
    char       net_dns2[APP_CONFIG_IPV4_MAXLEN];

    char       lorawan_deveui[APP_CONFIG_HEX8_LEN];
    char       lorawan_appeui[APP_CONFIG_HEX8_LEN];
    char       lorawan_appkey[APP_CONFIG_HEX16_LEN];

    /* Sleep mode (tiet kiem pin) - mac dinh TAT (sleep_enabled=false) de giu
     * nguyen hanh vi luon-bat hien tai cho thiet bi dang chay/moi flash. Khi
     * bat, main.c Deep Sleep giua cac chu ky, moi lan thuc publish 1 lan roi
     * ngu lai sleep_interval_ms. Xem main.c on_transport_ready()/app_main(). */
    bool       sleep_enabled;
    uint32_t   sleep_interval_ms;

    uint32_t   crc32;   /* computed over every field above, checked on load */
} app_config_t;

/* Doc NVS ("app_cfg"/"cfg_blob"). Neu chua tung luu (lan boot dau) hoac du lieu
 * hong/lech version/crc, dien *out bang gia tri mac dinh an toan (mode=WIFI_MQTT,
 * mqtt_port=1883, cac chuoi con lai rong) va tra ve ESP_ERR_NVS_NOT_FOUND - goi
 * y cho caller nen goi app_config_save() luon de ghi lai default do vao NVS. */
esp_err_t app_config_load(app_config_t *out);

/* Ghi *cfg vao NVS (tu tinh lai magic/version/crc32 truoc khi ghi). */
esp_err_t app_config_save(const app_config_t *cfg);

#ifdef __cplusplus
}
#endif
