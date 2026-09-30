/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#pragma once

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Khoi tao va bat REPL console tren UART0 (console mac dinh cua ESP-IDF),
 * dang ky toan bo lenh cau hinh (wifi_set/mqtt_set/lorawan_set/mode_set/...).
 * Goi 1 lan, DAU TIEN trong app_main() - truoc ca khi doc app_config va
 * truoc khi khoi tao WiFi hay LoRaWAN - de day la duong phuc hoi duy nhat
 * neu cau hinh da luu bi sai (SSID/pass sai, MQTT broker sai, LoRaWAN key
 * sai...) ma khong can reflash lai qua USB. Khong block, tu tao 1 task REPL
 * rieng chay nen. */
void cli_console_start(void);

/* Goi 1 lan sau khi main.c da doc app_config va biet chac dang chay o mode
 * nao trong phien nay - chi de lenh CLI 'mode_show' hien thi phan biet duoc
 * "mode da luu trong NVS" voi "mode dang thuc su chay" (khac nhau ngay sau
 * mot lenh 'mode_set' cho toi khi reboot). Khong bat buoc phai goi - neu
 * khong goi, 'mode_show' chi in mode da luu. */
void cli_console_set_running_mode(app_mode_t mode);

#ifdef __cplusplus
}
#endif
