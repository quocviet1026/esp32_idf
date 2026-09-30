/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* transport.h
 *
 * Lop truu tuong hoa tang ket noi (WiFi/MQTT hoac LoRaWAN) thanh 1 interface
 * duy nhat - dung struct chua con tro ham (khong co class/OOP trong C). Toan
 * bo code nghiep vu (main.c) chi biet goi qua "transport_if_t *", KHONG con
 * goi truc tiep esp_mqtt_client_publish()/rak3172_send_uplink() hay bat ky
 * API rieng nao cua tung transport nua.
 *
 * Mode duoc chon DUNG 1 LAN luc boot, dua theo app_config.mode (doc tu NVS
 * qua CLI console - xem components/app_config, components/cli_console),
 * KHONG dung DIP switch vat ly. Khong bao gio hot-switch giua 2 transport
 * trong 1 phien chay (doi mode qua lenh CLI 'mode_set' chi luu NVS, can
 * reboot moi ap dung - xem docs plan).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Danh sach loai ban tin ung dung gui LEN server - them loai moi chi can them
 * 1 dong enum + 1 nhanh trong union app_message_t.data (neu can du lieu kem
 * theo) + 1 nhanh dispatch trong publish() cua TUNG transport - KHONG bao gio
 * phai doi chu ky ham publish(). */
typedef enum {
    APP_MSG_SENSOR,          /* gia tri cam bien (gia lap) */
    APP_MSG_KEEPALIVE,       /* bao "thiet bi con song" o tang ung dung - KHONG phai MQTT protocol keepalive (PINGREQ/PINGRESP, da tu dong, khong lien quan o day) */
    APP_MSG_GOING_TO_SLEEP,  /* bao truoc khi vao Deep Sleep - CHI wire cho WiFi/MQTT (JSON), KHONG co ben LoRaWAN
                              * (giu dung tinh than "payload cang nho cang tot", xem transport_lorawan.c) - giong cach
                              * OTA chi wire o WiFi/MQTT. Xem main.c: chi publish loai nay NGAY TRUOC
                              * esp_deep_sleep_start(), sau khi da xac nhan an toan de ngu. */
} app_msg_type_t;

typedef struct {
    app_msg_type_t type;
    union {
        float sensor_value;         /* chi dung khi type == APP_MSG_SENSOR */
        /* APP_MSG_KEEPALIVE khong can du lieu kem theo */
        uint32_t sleep_interval_ms; /* chi dung khi type == APP_MSG_GOING_TO_SLEEP - de server biet du kien bao lau nua thiet bi moi lien lac lai */
    } data;
} app_message_t;

/* Danh sach loai lenh/ban tin nhan duoc TU SERVER XUONG thiet bi - doi xung
 * voi app_msg_type_t (chieu nguoc lai). Hien CHI co APP_CMD_UNKNOWN (chua co
 * nghiep vu cu the nao duoc dinh nghia) - them loai lenh that: them 1 dong
 * enum + 1 nhanh trong union app_command_t.data + 1 nhanh dispatch trong
 * TUNG transport (giong het cach app_msg_type_t/s_publish_routes[] da lam cho
 * chieu len), KHONG bao gio phai doi chu ky transport_downlink_cb_t. */
typedef enum {
    APP_CMD_UNKNOWN = 0,   /* khong nhan dien duoc loai lenh, hoac chua co nghiep vu rieng - xem data.raw */
} app_cmd_type_t;

typedef struct {
    app_cmd_type_t type;
    union {
        /* Dung khi type == APP_CMD_UNKNOWN (hoac bat ky loai nao chua co
         * truong rieng): du lieu tho chua decode, de nguoi nhan (main.c) tu
         * xem neu can. CANH BAO: con tro "payload" chi hop le TRONG LUC
         * callback dang chay - transport tu giai phong/ghi de buffer ngay
         * sau khi callback return, KHONG duoc luu con tro nay lai dung sau. */
        struct {
            const uint8_t *payload;
            size_t len;
        } raw;
    } data;
} app_command_t;

typedef struct {
    /* Bat dau ket noi (KHONG block) - vd WiFi connect roi doi MQTT connect,
     * hoac rak3172_init()+configure_identity()+join(). Ket qua "da san sang"
     * duoc bao qua callback dang ky boi transport_set_ready_callback(),
     * tuong duong buoc "handshake xong" trong yeu cau goc. */
    esp_err_t (*start)(const app_config_t *cfg);

    /* Gui 1 ban tin ung dung len server. MOI transport TU QUYET DINH cach ma
     * hoa/dong goi/chon topic-hoac-port phu hop nhat cho tung app_msg_type_t
     * (vd JSON text qua MQTT, hay fixed-point vai byte qua LoRaWAN de tiet
     * kiem airtime) - day la ly do truyen "du lieu tho" (union) thay vi 1
     * buffer da encode san: ep tat ca transport dung chung 1 dinh dang se lam
     * mat toi uu rieng cua tung ben.
     *
     * LUU Y: ham nay KHONG tu lap lai/tu dinh ky - moi lan goi la gui DUNG 1
     * LAN, ngay lap tuc. Viec "bao lau goi lai 1 lan" (interval) hoan toan do
     * NGUOI GOI (main.c) tu quyet dinh va tu vong lap lay - transport khong
     * biet gi ve chu ky ca, giong cach coreMQTT/Azure IoT SDK khong he co
     * khai niem "interval" trong ham publish/send cua ho. Ly do: interval an
     * toan (dac biet cho LoRaWAN, gioi han boi duty-cycle) la 1 quyet dinh
     * NGHIEP VU/CAU HINH, khong phai co che truyen tai - dat no trong
     * transport_if_t se tron lan "co che" (how) voi "chinh sach" (how often). */
    esp_err_t (*publish)(const app_message_t *msg);

    /* true khi transport da san sang gui du lieu (MQTT dang connected, hoac
     * LoRaWAN da joined). */
    bool (*is_ready)(void);
} transport_if_t;

/* Callback goi DUNG 1 LAN DUY NHAT trong ca vong doi thiet bi, ngay khi
 * transport lan dau tien san sang (MQTT CONNECTED lan dau, hoac LoRaWAN
 * JOINED lan dau) - kha nang "chi bao 1 lan" nay do transport.c tu dam bao
 * (xem transport_internal.h), khong phai trach nhiem cua tung implementation -
 * vi du MQTT co the disconnect/reconnect nhieu lan, nhung callback nay se
 * KHONG goi lai o nhung lan reconnect sau, tranh sinh trung task cam bien. */
typedef void (*transport_ready_cb_t)(void);

/* Chon dung 1 trong 2 implementation theo app_config.mode - goi DUNG 1 LAN
 * trong app_main(), khong bao gio goi lai/doi ket qua giua chung 1 phien
 * chay. */
const transport_if_t *transport_get(app_mode_t mode);

/* Dang ky callback "san sang lan dau" - PHAI goi TRUOC transport->start(),
 * neu khong co the bo lo lan bao dau tien. */
void transport_set_ready_callback(transport_ready_cb_t cb);

/* Callback nhan 1 lenh/ban tin TU SERVER XUONG - KHONG co gioi han "chi goi 1
 * lan" nhu transport_ready_cb_t (downlink co the toi bat ky luc nao, nhieu
 * lan). Chay trong context cua chinh transport dang hoat dong (task noi bo
 * cua esp-mqtt, hoac RX task cua rak3172) - KHONG duoc block lau trong ham
 * nay, giong het rang buoc cua rak3172_downlink_cb_t. */
typedef void (*transport_downlink_cb_t)(const app_command_t *cmd);

/* Dang ky callback nhan lenh tu server - nen goi TRUOC transport->start(),
 * giong transport_set_ready_callback(), de khong bo lo ban tin nao toi som. */
void transport_set_downlink_callback(transport_downlink_cb_t cb);

#ifdef __cplusplus
}
#endif
