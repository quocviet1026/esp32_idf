/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* transport_lorawan.c
 *
 * Implementation cua transport_if_t cho LoRaWAN (qua RAK3172) - gop toan bo
 * logic truoc day nam trong main.c (rak3172_init/configure_identity/join,
 * on_lorawan_joined/downlink) vao 1 noi duy nhat. KHONG co dong goi
 * ota_manager_start() nao o day - day chinh la cach hien thuc hoa "OTA khong
 * ton tai o mode LoRaWAN" (khong co duong goi, khong phai guard runtime).
 */

#include "esp_log.h"

#include "app_config.h"
#include "rak3172.h"
#include "transport_internal.h"

static const char *TAG = "transport_lorawan";

/* Port ung dung LoRaWAN cho tung loai ban tin - phan biet o phia server/
 * network server dua tren port nay (giong vai tro cua "topic" ben MQTT). */
#define LORAWAN_PORT_SENSOR    2
#define LORAWAN_PORT_KEEPALIVE 1

/* LUU Y: interval publish (bao lau goi transport->publish() 1 lan) KHONG con
 * nam o day nua - do main.c tu quyet dinh va tu vong lap (xem thao luan thiet
 * ke: "khi nao goi" la viec cua app, transport chi la co che gui). Neu ban
 * doi sang vung tan so/Data Rate khac lam thay doi gioi han duty-cycle an
 * toan, chi can sua bang chu ky trong main.c, KHONG can dong gi o file nay. */

static void on_lorawan_joined(bool joined)
{
    if (joined) {
        transport_notify_ready();
    } else {
        ESP_LOGE(TAG, "LoRaWAN join failed - dung lenh CLI 'lorawan_join' de thu lai (khong can reboot), "
                      "hoac 'lorawan_show' de kiem tra lai DevEUI/AppEUI/AppKey");
    }
}

static void on_lorawan_downlink(uint8_t port, const uint8_t *payload, size_t len, int rssi, int snr)
{
    /* Chua co lenh cu the nao duoc dinh nghia theo tung PORT rieng (xem
     * app_cmd_type_t trong transport.h) - tam thoi chuyen tiep NGUYEN VAN moi
     * downlink (bat ke port nao) thanh app_command_t kieu APP_CMD_UNKNOWN, de
     * main.c tu quyet dinh. Them lenh cu the: switch theo "port" o day, giong
     * cach s_publish_routes[] dispatch theo type o chieu len - vd 1 port
     * rieng cho lenh dieu khien, khac han port cua sensor/keepalive. */
    ESP_LOGI(TAG, "Downlink nhan duoc: port=%u len=%u rssi=%d snr=%d", port, (unsigned)len, rssi, snr);

    app_command_t cmd = {
        .type = APP_CMD_UNKNOWN,
        .data.raw = { .payload = payload, .len = len },
    };
    transport_notify_downlink(&cmd);
}

static esp_err_t lorawan_start(const app_config_t *cfg)
{
    rak3172_config_t rak_cfg = {
        .uart_num   = CONFIG_RAK3172_UART_NUM,
        .tx_gpio    = CONFIG_RAK3172_TX_GPIO,
        .rx_gpio    = CONFIG_RAK3172_RX_GPIO,
        .reset_gpio = CONFIG_RAK3172_RESET_GPIO,
        .baud_rate  = 115200,
    };

    esp_err_t err = rak3172_init(&rak_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rak3172_init that bai (%s) - kiem tra menuconfig Component config -> RAK3172 "
                      "da dat dung RAK3172_TX_GPIO/RX_GPIO cho board that chua", esp_err_to_name(err));
        return err;
    }

    rak3172_set_join_callback(on_lorawan_joined);
    rak3172_set_downlink_callback(on_lorawan_downlink);

    /* Best-effort: bat low-power khong duoc thi van tiep tuc hoat dong binh
     * thuong (chi mat phan tiet kiem pin, khong anh huong den viec join/gui
     * ban tin) - khong return loi o day. */
    err = rak3172_enable_low_power();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rak3172_enable_low_power that bai (%s) - van tiep tuc, chi mat phan tiet kiem pin cua module",
                 esp_err_to_name(err));
    }

    err = rak3172_configure_identity(cfg->lorawan_deveui, cfg->lorawan_appeui, cfg->lorawan_appkey,
                                      CONFIG_RAK3172_BAND_INDEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rak3172_configure_identity that bai (%s) - kiem tra 'lorawan_show' da co du "
                      "DevEUI/AppEUI/AppKey chua, va menuconfig RAK3172_BAND_INDEX da dat dung band chua",
                 esp_err_to_name(err));
        return err;
    }

    /* Module KHONG bi cat nguon giua cac chu ky Deep Sleep cua ESP32 (xem
     * rak3172_enable_low_power()) nen rat co the van con giu session OTAA cu -
     * hoi truoc bang AT+NJS=? de tranh rejoin ton airtime/pin khong can thiet.
     * Neu cau lenh nay that bai (vd module vua duoc cap nguon lan dau, chua on
     * dinh UART) thi cu coi nhu "chua joined" va join binh thuong nhu cu, an
     * toan hon la bo qua buoc join. */
    bool already_joined = false;
    err = rak3172_query_join_status(&already_joined);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rak3172_query_join_status that bai (%s), coi nhu chua joined va join lai binh thuong",
                 esp_err_to_name(err));
        already_joined = false;
    }

    if (already_joined) {
        ESP_LOGI(TAG, "Module bao da joined san (AT+NJS=?=1) - bo qua AT+JOIN, tiet kiem airtime/pin");
        on_lorawan_joined(true);
        return ESP_OK;
    }

    err = rak3172_join();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Gui AT+JOIN that bai: %s", esp_err_to_name(err));
    }
    return err;
}

/* ===================== Bang publish theo loai ban tin ===================== */
/*
 * Cung 1 triet ly voi transport_wifi_mqtt.c: moi loai ban tin = 1 ham encode
 * NHO, DOC LAP, chi lo "dien byte payload cho loai nay". Them/sua 1 loai ban
 * tin chi dung sua/them DUNG 1 ham encode_xxx_lorawan() + 1 dong trong
 * s_publish_routes[] - khong con phai doc/sua 1 khoi switch chung.
 *
 * Encoder tra ve esp_err_t (thay vi void nhu ben MQTT) vi payload LoRaWAN co
 * gioi han kich thuoc cung (buffer co dinh out_cap) - can bao loi ro rang neu
 * 1 loai ban tin nao do lo ma hoa vuot qua buffer, thay vi tran bo nho im lang.
 */
typedef esp_err_t (*lorawan_payload_encoder_t)(const app_message_t *msg, uint8_t *out, size_t out_cap, size_t *out_len);

static esp_err_t encode_sensor_payload(const app_message_t *msg, uint8_t *out, size_t out_cap, size_t *out_len)
{
    /* Fixed-point 4 byte (2 chu so thap phan) thay vi JSON text - tiet kiem
     * airtime, quan trong voi LoRaWAN (payload cang nho cang tot). */
    if (out_cap < 4) {
        return ESP_ERR_INVALID_SIZE;
    }
    int32_t fixed = (int32_t)(msg->data.sensor_value * 100.0f);
    out[0] = (uint8_t)((fixed >> 24) & 0xFF);
    out[1] = (uint8_t)((fixed >> 16) & 0xFF);
    out[2] = (uint8_t)((fixed >> 8) & 0xFF);
    out[3] = (uint8_t)(fixed & 0xFF);
    *out_len = 4;
    return ESP_OK;
}

static esp_err_t encode_keepalive_payload(const app_message_t *msg, uint8_t *out, size_t out_cap, size_t *out_len)
{
    /* 1 byte danh dau "con song" - toi thieu co the gui qua LoRaWAN
     * (rak3172_send_uplink yeu cau len >= 1), khong can noi dung gi them vi
     * ban than viec uplink toi noi da la bang chung "con song". */
    (void)msg;   /* khong dung du lieu gi tu msg, chi de tham so dong nhat voi cac encoder khac */
    if (out_cap < 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    out[0] = 0x01;
    *out_len = 1;
    return ESP_OK;
}

typedef struct {
    app_msg_type_t type;
    uint8_t port;
    lorawan_payload_encoder_t encode;
} lorawan_publish_route_t;

static const lorawan_publish_route_t s_publish_routes[] = {
    { APP_MSG_SENSOR,    LORAWAN_PORT_SENSOR,    encode_sensor_payload },
    { APP_MSG_KEEPALIVE, LORAWAN_PORT_KEEPALIVE, encode_keepalive_payload },
    /* Them loai ban tin moi: { APP_MSG_XXX, PORT_XXX, encode_xxx_payload }, */
};
#define PUBLISH_ROUTE_COUNT (sizeof(s_publish_routes) / sizeof(s_publish_routes[0]))

static esp_err_t lorawan_publish(const app_message_t *msg)
{
    for (size_t i = 0; i < PUBLISH_ROUTE_COUNT; i++) {
        if (s_publish_routes[i].type != msg->type) {
            continue;
        }
        uint8_t payload[16];   /* du lon cho moi loai ban tin hien tai (toi da 4 byte) */
        size_t len = 0;
        esp_err_t err = s_publish_routes[i].encode(msg, payload, sizeof(payload), &len);
        if (err != ESP_OK) {
            return err;
        }
        return rak3172_send_uplink(s_publish_routes[i].port, payload, len, 5000);
    }

    ESP_LOGW(TAG, "publish(): app_msg_type_t %d chua duoc dang ky trong s_publish_routes[]", msg->type);
    return ESP_ERR_NOT_SUPPORTED;
}

static bool lorawan_is_ready(void)
{
    return rak3172_get_join_state() == RAK3172_JOIN_JOINED;
}

const transport_if_t g_transport_lorawan = {
    .start    = lorawan_start,
    .publish  = lorawan_publish,
    .is_ready = lorawan_is_ready,
};
