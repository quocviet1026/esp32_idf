/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* Header noi bo dung chung giua transport.c va 2 file implementation
 * (transport_wifi_mqtt.c, transport_lorawan.c) - KHONG public, main.c khong
 * duoc include file nay. */
#pragma once

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cac struct implementation cu the - dinh nghia trong transport_wifi_mqtt.c
 * va transport_lorawan.c, transport_get() chi tra ve dia chi cua 1 trong 2. */
extern const transport_if_t g_transport_wifi_mqtt;
extern const transport_if_t g_transport_lorawan;

/* Goi boi implementation dang chay (tu mqtt_event_handler khi CONNECTED lan
 * dau, hoac tu on_lorawan_joined khi JOINED lan dau) de bao "transport da san
 * sang". transport.c tu dam bao chi thuc su goi callback cua nguoi dung DUNG
 * 1 LAN du ham nay co the duoc goi lai nhieu lan (vd MQTT reconnect nhieu
 * lan) - cac implementation KHONG can tu lo viec "chi bao 1 lan", cu goi moi
 * khi transport that su san sang la du. */
void transport_notify_ready(void);

/* Goi boi implementation dang chay moi khi decode duoc 1 lenh/ban tin tu
 * server (vd trong handle_command_topic() cua transport_wifi_mqtt.c, hoac
 * on_lorawan_downlink() cua transport_lorawan.c) - chuyen tiep NGAY cho
 * callback nguoi dung (neu co dang ky). KHONG co co che "chi bao 1 lan" nhu
 * transport_notify_ready() - downlink hop le moi lan, khong phai su kien
 * "mot lan trong doi". */
void transport_notify_downlink(const app_command_t *cmd);

#ifdef __cplusplus
}
#endif
