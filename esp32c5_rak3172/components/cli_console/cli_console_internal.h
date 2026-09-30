/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* Header noi bo cua component cli_console - KHONG nam trong include/ (khong
 * public), chi de cli_console.c goi cac ham register_xxx_cmds() dinh nghia
 * trong tung file cmd_*.c. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void register_wifi_cfg_cmds(void);
void register_mqtt_cfg_cmds(void);
void register_lorawan_cfg_cmds(void);
void register_mode_cmds(void);
void register_system_cmds(void);
void register_net_cfg_cmds(void);
void register_sleep_cfg_cmds(void);

#ifdef __cplusplus
}
#endif
