/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cau hinh IP tinh, tuy chon - truyen NULL cho wifi_manager_start() de dung
 * DHCP (hanh vi mac dinh, khong doi gi). Cac chuoi IP la ASCII dang
 * "192.168.1.100" - wifi_manager tu parse/validate bang esp_netif_str_to_ip4(),
 * KHONG can validate truoc o noi goi (du CLI da validate 1 lan roi - day la
 * lop kiem tra thu 2, phong thu, giong nguyen tac ota_manager tu kiem tra lai
 * topic du dispatch table da loc truoc do). dns1/dns2 co the la NULL hoac ""
 * neu khong dat DNS tinh. */
typedef struct {
    const char *ip;
    const char *netmask;
    const char *gateway;
    const char *dns1;
    const char *dns2;
} wifi_static_ip_config_t;

/* Ket noi WiFi STA voi ssid/password truyen vao (doc tu app_config, KHONG
 * hardcode - khac voi ban goc trong /home/vietnq/esp/ota). Goi 1 lan, khong
 * block - ket qua bao qua IP_EVENT_STA_GOT_IP (dang ky rieng o noi goi, xem
 * main.c), giong het pattern cua ban goc. static_ip == NULL -> DHCP. */
void wifi_manager_start(const char *ssid, const char *password, const wifi_static_ip_config_t *static_ip);

#ifdef __cplusplus
}
#endif
