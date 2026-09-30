/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#pragma once

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_COMMAND_TOPIC "esp32/vietnq/ota/cmd"
#define OTA_STATUS_TOPIC  "esp32/vietnq/ota/status"

/* Subscribe to OTA_COMMAND_TOPIC and remember the client used to publish status.
 * Call once, right after the MQTT client connects. */
void ota_manager_start(esp_mqtt_client_handle_t client);

/* Feed every MQTT_EVENT_DATA event here. Events on topics other than
 * OTA_COMMAND_TOPIC are ignored. */
void ota_manager_handle_mqtt_data(const esp_mqtt_event_handle_t event);

/* Called by ota_task once an OTA attempt ends without rebooting (i.e. it failed),
 * so a later command can trigger OTA again. */
void ota_manager_notify_task_done(void);

#ifdef __cplusplus
}
#endif
