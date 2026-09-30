/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#pragma once

#include <stdbool.h>
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_COMMAND_TOPIC "esp32c5_rak3172/vietnq/ota/cmd"
#define OTA_STATUS_TOPIC  "esp32c5_rak3172/vietnq/ota/status"

/* Subscribe to OTA_COMMAND_TOPIC and remember the client used to publish status.
 * Call once, right after the MQTT client connects. */
void ota_manager_start(esp_mqtt_client_handle_t client);

/* Feed every MQTT_EVENT_DATA event here. Events on topics other than
 * OTA_COMMAND_TOPIC are ignored. */
void ota_manager_handle_mqtt_data(const esp_mqtt_event_handle_t event);

/* Called by ota_task once an OTA attempt ends without rebooting (i.e. it failed),
 * so a later command can trigger OTA again. */
void ota_manager_notify_task_done(void);

/* True tu luc nhan lenh OTA hop le (truoc khi ota_task bat dau tai/ghi
 * firmware) cho toi khi: (a) OTA thanh cong -> thiet bi reboot ngay, KHONG
 * bao gio quay lai false trong phien chay nay (khong can thiet, vi thiet bi
 * da restart); hoac (b) OTA that bai -> ota_manager_notify_task_done() dat lai
 * false. Dung de cac phan khac cua firmware (vd sleep mode trong main.c) biet
 * "dang co qua trinh OTA active hay khong" TRUOC KHI quyet dinh lam gi do co
 * the ngat quang no giua chung (nhu Deep Sleep, cat nguon dot ngot giua luc
 * dang ghi flash). */
bool ota_manager_is_busy(void);

#ifdef __cplusplus
}
#endif
