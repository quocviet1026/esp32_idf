/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int uart_num;      /* UART_NUM_1 hoac UART_NUM_2 - KHONG dung UART_NUM_0 (console) */
    int tx_gpio;
    int rx_gpio;
    int reset_gpio;    /* -1 neu khong noi chan RESET cua module ra GPIO nao */
    int baud_rate;      /* RAK3172 RUI3 mac dinh 115200 */
} rak3172_config_t;

typedef enum {
    RAK3172_JOIN_IDLE = 0,
    RAK3172_JOIN_JOINING,
    RAK3172_JOIN_JOINED,
    RAK3172_JOIN_FAILED,
} rak3172_join_state_t;

/* Goi tu RX task noi bo ngay khi thay dong "+EVT:RX_1:..." - KHONG duoc block
 * lau trong callback nay (dang chay trong context cua task doc UART, block se
 * lam nghen viec doc cac dong tiep theo tu module). */
typedef void (*rak3172_downlink_cb_t)(uint8_t port, const uint8_t *payload, size_t len, int rssi, int snr);

/* Goi tu RX task noi bo khi thay "+EVT:JOINED" hoac "+EVT:JOIN_FAILED...". */
typedef void (*rak3172_join_cb_t)(bool joined);

/* Khoi tao UART + bat task doc/tach dong phan hoi. Goi 1 lan truoc moi ham
 * rak3172_* khac. Cac ham khac tra ve ESP_ERR_INVALID_STATE neu goi truoc khi
 * init (vd: dang chay o mode WIFI_MQTT, rak3172_init() khong duoc goi trong
 * boot do - cac lenh CLI lorawan_join/lorawan_status can tu kiem tra loi nay
 * de bao ro rang thay vi crash). */
esp_err_t rak3172_init(const rak3172_config_t *cfg);

/* Gui 1 lenh AT tho (KHONG kem "\r\n" - driver tu them), cho toi da timeout_ms
 * de nhan dong ket thuc dong bo (OK/AT_ERROR/...). resp_buf/resp_buf_len co
 * the NULL/0 neu khong can noi dung phan hoi (vd lenh chi tra ve OK). Chi 1
 * lenh duoc phep "in flight" tai 1 thoi diem (AT protocol khong pipeline
 * duoc) - ham nay tu khoa bang mutex noi bo, an toan goi tu nhieu task khac
 * nhau (CLI, main, sensor task) nhung KHONG duoc goi tu ben trong callback
 * cua chinh rak3172 (se deadlock voi RX task). */
esp_err_t rak3172_send_cmd(const char *cmd, char *resp_buf, size_t resp_buf_len, uint32_t timeout_ms);

/* Cau hinh 1 lan: AT+NWM=1, AT+BAND=<band_index>, AT+NJM=1, AT+DEVEUI/APPEUI/APPKEY,
 * AT+CLASS=A, AT+CFM=0. Idempotent - goi lai moi boot van an toan (module chi
 * nhan lai cung gia tri). deveui_hex/appeui_hex/appkey_hex la chuoi hex ASCII
 * (khong co "0x", khong dau cach), band_index theo bang AT+BAND cua dung ban
 * firmware RUI3 tren module - KHONG doan, xem README/Kconfig cua component nay. */
esp_err_t rak3172_configure_identity(const char *deveui_hex, const char *appeui_hex,
                                      const char *appkey_hex, int band_index);

/* AT+JOIN=1:0:10:8 - CHI la async trigger: OK nghia la "da bat dau qua trinh
 * join", KHONG phai "da join xong". Ket qua that su bao qua rak3172_join_cb_t
 * (hoac poll rak3172_get_join_state()) khi RX task thay "+EVT:JOINED"/
 * "+EVT:JOIN_FAILED...". Khong block. */
esp_err_t rak3172_join(void);

rak3172_join_state_t rak3172_get_join_state(void);

void rak3172_set_join_callback(rak3172_join_cb_t cb);

/* AT+SEND=<port>:<hex(payload)> - driver tu hex-encode payload, caller truyen
 * byte tho. */
esp_err_t rak3172_send_uplink(uint8_t port, const uint8_t *payload, size_t len, uint32_t timeout_ms);

void rak3172_set_downlink_callback(rak3172_downlink_cb_t cb);

typedef struct {
    bool    valid;   /* false neu chua tung nhan downlink nao tu luc boot */
    uint8_t port;
    int     rssi;
    int     snr;
} rak3172_downlink_info_t;

/* Thong tin downlink GAN NHAT (de lenh CLI 'lorawan_status' hien thi) - khong
 * lien quan gi den rak3172_downlink_cb_t (2 co che doc lap, callback de xu ly
 * ngay, ham nay de "hoi lai" trang thai bat ky luc nao). */
void rak3172_get_last_downlink_info(rak3172_downlink_info_t *out);

/* ATZ - soft reset module (can UART con phan hoi). */
esp_err_t rak3172_reset_module(void);

/* AT+LPM=1 roi AT+LPMLVL=1 (STOP1 - cho phep UART danh thuc, khac STOP2 khong
 * danh thuc duoc qua UART). Bat 1 lan luc lorawan_start() de module tu ngu
 * giua cac lenh AT khi ranh, tu thuc khi nhan byte UART moi tu ESP32 o chu ky
 * thuc tiep theo (sau khi ESP32 Deep Sleep). Module KHONG bi cat nguon (van do
 * ESP32 cap nguon lien tuc) nen khong can dong bo thoi gian ngu chinh xac giua
 * 2 chip - xem "Viec can xac nhan tren phan cung that" trong plan sleep mode:
 * CHUA CO tai lieu RUI3 nao xac nhan co che auto-sleep nay co doi giao dich
 * LoRaWAN dang do dang (uplink + dong cua so RX1/RX2) xong roi moi thuc su vao
 * STOP1 hay khong - BAT BUOC bench test do dong tieu thu that truoc khi dung
 * production. */
esp_err_t rak3172_enable_low_power(void);

/* AT+NJS=? - hoi module co dang giu session join hay khong (0/1), KHONG dua
 * vao trang thai cache o phia ESP32 (rak3172_get_join_state() chi phan anh
 * trang thai RAM cua ESP32, bi mat moi lan ESP32 Deep Sleep). Vi RAK3172 khong
 * bi cat nguon nen co the van con giu session OTAA qua cac chu ky ngu cua
 * ESP32 - goi ham nay TRUOC rak3172_join() de tranh rejoin OTAA khong can
 * thiet (ton airtime + pin) neu module bao da joined roi. */
esp_err_t rak3172_query_join_status(bool *out_joined);

#ifdef __cplusplus
}
#endif
