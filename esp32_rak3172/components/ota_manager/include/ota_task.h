/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#pragma once

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn a FreeRTOS task that downloads and applies the firmware at `url` via
 * esp_https_ota, publishing progress on OTA_STATUS_TOPIC through mqtt_client.
 * Takes ownership of nothing; makes its own copy of `url`. */
void ota_task_start(esp_mqtt_client_handle_t mqtt_client, const char *url);

#ifdef __cplusplus
}
#endif
