/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* transport_wifi_mqtt.c
 *
 * Implementation cua transport_if_t cho WiFi + MQTT - gop toan bo logic
 * truoc day nam rai rac trong main.c (mqtt_start, mqtt_event_handler,
 * ip_event_handler) vao 1 noi duy nhat. Day cung la noi DUY NHAT wire OTA
 * (ota_manager_start/handle_mqtt_data/rollback) - LoRaWAN KHONG co tuong
 * duong, dung nhu quyet dinh thiet ke "OTA chi hoat dong o mode WiFi/MQTT".
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "esp_app_desc.h"

#include "app_config.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "ota_rollback.h"
#include "transport_internal.h"

static const char *TAG = "transport_wifi_mqtt";

#define VERSION_TOPIC   "esp32_rak3172/vietnq/version"
#define SENSOR_TOPIC    "esp32_rak3172/vietnq/sensor"
#define KEEPALIVE_TOPIC "esp32_rak3172/vietnq/keepalive"
#define SLEEP_TOPIC     "esp32_rak3172/vietnq/sleep"

/* Neu sau tung nay ms ke tu luc bat dau ket noi WiFi ma van CHUA confirm
 * duoc rollback (vd WiFi/MQTT khong bao gio ket noi duoc du khong crash),
 * ota_rollback se CHU DONG rollback ve firmware cu thay vi treo vo thoi han
 * (xem ota_rollback_start_confirm_watchdog() va thao luan thiet ke). 2 phut
 * du rong rai cho ca WiFi retry (toi da 5 lan, xem wifi_manager.c) lan MQTT
 * connect trong dieu kien mang binh thuong. */
#define ROLLBACK_CONFIRM_TIMEOUT_MS (1 * 60 * 1000)

static esp_mqtt_client_handle_t s_mqtt_client;
static app_config_t             s_cfg;   /* ban sao rieng, dung khi IP_EVENT toi de biet broker nao ma khong can main.c truyen lai */
static volatile bool            s_mqtt_connected;   /* true CHI trong khoang giua MQTT_EVENT_CONNECTED va lan DISCONNECTED/ERROR ke tiep - xem wifi_mqtt_is_ready() */

/* ===================== Dispatch table cho MQTT_EVENT_DATA ===================== */
/*
 * Thay vi goi tran het moi component vo dieu kien roi de tung component tu
 * loc topic cua no (cach lam cu), o day tra bang 1 lan de tim DUNG 1 handler
 * khop voi topic thuc te cua bai tin, roi CHI goi handler do. Loi ich khi so
 * topic tang len:
 *   - Moi topic moi = them DUNG 1 dong vao s_mqtt_routes[], khong phai sua
 *     logic trong case MQTT_EVENT_DATA.
 *   - Neu bai tin toi tren 1 topic ma KHONG co route nao khop, se duoc LOG
 *     RO RANG ("unhandled topic") thay vi am tham bien mat - lo hong nay
 *     ton tai truoc day du chi co 1 topic.
 *
 * ota_manager_handle_mqtt_data() van tu kiem tra lai topic ben trong no (xem
 * ota_manager.c) - gio thanh 1 lop kiem tra "thua" vo hai (defense-in-depth),
 * KHONG sua ota_manager (component copy nguyen ven tu esp/ota, xem
 * docs/PORTING_GUIDE.md) vi no van portable duoc sang project khac ma khong
 * phu thuoc gi vao co che dispatch table nay.
 */
typedef void (*mqtt_topic_handler_t)(const esp_mqtt_event_handle_t event);

typedef struct {
    const char *topic;
    mqtt_topic_handler_t handler;
} mqtt_topic_route_t;

/* Topic nhan lenh chung tu server (KHAC OTA_COMMAND_TOPIC - topic do danh
 * rieng cho ota_manager, khong lien quan nghiep vu). Component nay tu
 * subscribe topic cua chinh no (xem case MQTT_EVENT_CONNECTED ben duoi),
 * giong het cach ota_manager tu subscribe OTA_COMMAND_TOPIC cua no. */
#define COMMAND_TOPIC "esp32_rak3172/vietnq/cmd"

/* Chua co lenh cu the nao duoc dinh nghia (xem app_cmd_type_t trong
 * transport.h) - tam thoi chuyen tiep NGUYEN VAN payload dang app_command_t
 * kieu APP_CMD_UNKNOWN, de main.c tu quyet dinh xu ly gi khi co nghiep vu
 * that. Them lenh cu the: parse JSON o day (vd cJSON, giong ota_manager.c),
 * map sang 1 app_cmd_type_t rieng thay vi luon tra ve UNKNOWN. */
static void handle_command_topic(const esp_mqtt_event_handle_t event)
{
    app_command_t cmd = {
        .type = APP_CMD_UNKNOWN,
        .data.raw = {
            .payload = (const uint8_t *)event->data,
            .len = (size_t)event->data_len,
        },
    };
    transport_notify_downlink(&cmd);
}

static const mqtt_topic_route_t s_mqtt_routes[] = {
    { OTA_COMMAND_TOPIC, ota_manager_handle_mqtt_data },
    { COMMAND_TOPIC,     handle_command_topic },
    /* Them topic moi: { TEN_TOPIC_MOI, ham_xu_ly_moi }, - nho subscribe topic
     * do o dau (thuong trong ham *_start() cua component tuong ung, giong
     * cach ota_manager_start() tu subscribe OTA_COMMAND_TOPIC). */
};
#define MQTT_ROUTE_COUNT (sizeof(s_mqtt_routes) / sizeof(s_mqtt_routes[0]))

static void mqtt_dispatch_data(const esp_mqtt_event_handle_t event)
{
    for (size_t i = 0; i < MQTT_ROUTE_COUNT; i++) {
        size_t topic_len = strlen(s_mqtt_routes[i].topic);
        if ((size_t)event->topic_len == topic_len &&
            strncmp(event->topic, s_mqtt_routes[i].topic, topic_len) == 0) {
            s_mqtt_routes[i].handler(event);
            return;
        }
    }
    ESP_LOGW(TAG, "Khong co handler nao dang ky cho topic [%.*s] - bo qua bai tin",
             event->topic_len, event->topic);
}

/* Handler DUY NHAT cho MOI su kien cua esp-mqtt (dang ky qua ESP_EVENT_ANY_ID
 * trong mqtt_start() ben duoi) - chay trong task noi bo cua esp-mqtt, KHONG
 * phai task "main" hay ISR. esp-mqtt phat cac su kien nay TUAN TU (khong bao
 * gio 2 event chay dong thoi), nen doc/ghi cac bien static trong file nay
 * (s_mqtt_connected, s_mqtt_client) khong can mutex - xem giai thich tuong tu
 * o cuoi transport.c ve s_ready_fired. */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected successfully");
        s_mqtt_connected = true;   /* xem wifi_mqtt_is_ready() - dat NGAY DAU case nay, truoc bat ky lenh publish/subscribe nao ben duoi */
        {
            /* Publish version MOI LAN connect (ke ca reconnect, khong chi lan
             * dau) - giup server/dashboard luon biet dung firmware nao dang
             * thuc su chay tren thiet bi ngay khi vua ket noi lai, huu ich de
             * phat hien thiet bi vo tinh chay firmware cu sau 1 lan rollback. */
            const esp_app_desc_t *app_desc = esp_app_get_description();
            char version_payload[64];
            snprintf(version_payload, sizeof(version_payload), "{\"version\":\"%s\"}", app_desc->version);
            esp_mqtt_client_publish(client, VERSION_TOPIC, version_payload, 0, 1, 0);   /* len=0 (tu tinh bang strlen), qos=1, retain=0 */
            ESP_LOGI(TAG, "Published %s to %s", version_payload, VERSION_TOPIC);
        }

        /* Subscribe topic nhan lenh cua chinh component nay - an toan goi lai
         * moi lan reconnect, esp-mqtt tu bo qua neu da subscribe topic do roi. */
        esp_mqtt_client_subscribe(client, COMMAND_TOPIC, 1);
        ESP_LOGI(TAG, "Subscribed to command topic: %s", COMMAND_TOPIC);

        /* An toan goi lai moi lan reconnect (xem doc trong ota_manager.h) -
         * KHONG dung transport_notify_ready() de guard 2 dong nay, chi guard
         * viec khoi tao task cam bien (thuc hien ben trong notify_ready).
         * ota_rollback_confirm_if_pending() la buoc XAC NHAN rollback THAT SU
         * (dang chay, KHONG duoc phep comment ra o production) - neu firmware
         * vua OTA xong ma khong bao gio toi duoc dong nay, watchdog
         * ota_rollback_start_confirm_watchdog() (goi trong wifi_mqtt_start())
         * se tu rollback ve firmware cu sau ROLLBACK_CONFIRM_TIMEOUT_MS. */
        ota_manager_start(client);
        ota_rollback_confirm_if_pending();

        transport_notify_ready();
        break;

    case MQTT_EVENT_DISCONNECTED:
        /* esp-mqtt tu dong thu ket noi lai (khong can code o day goi lai
         * esp_mqtt_client_start()) - chi can dat lai co trang thai. Neu
         * reconnect thanh cong sau do, se lai vao case CONNECTED o tren, VA
         * transport_notify_ready() se KHONG goi lai s_ready_cb() lan 2 (xem
         * co che "chi bao 1 lan" trong transport.c). */
        ESP_LOGW(TAG, "MQTT disconnected, esp-mqtt se tu dong thu ket noi lai");
        s_mqtt_connected = false;
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Received message on topic [%.*s]: %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        mqtt_dispatch_data(event);
        break;

    case MQTT_EVENT_ERROR:
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "MQTT TCP/TLS transport error: esp_err=0x%x, tls_stack_err=%d, sock_errno=%d",
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_tls_stack_err,
                     event->error_handle->esp_transport_sock_errno);
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "MQTT connection refused by broker, return_code=%d",
                     event->error_handle->connect_return_code);
        } else {
            ESP_LOGE(TAG, "MQTT error, error_type=%d", event->error_handle->error_type);
        }
        break;

    default:
        break;
    }
}

/* Goi 1 lan khi WiFi vua co IP (xem ip_event_handler ben duoi). Dung
 * "mqtt://" (khong TLS) - project nay chua ho tro MQTTS/xac thuc server cert,
 * xem KNOWN_ISSUES.md neu can nang cap len bao mat hon truoc khi dung that
 * ngoai field. username/password de NULL (khong phai chuoi rong "") khi
 * chua cau hinh - esp-mqtt phan biet 2 truong hop nay khac nhau (NULL = bo
 * qua xac thuc, chuoi rong van la 1 gia tri hop le se gui len broker). */
static void mqtt_start(void)
{
    char uri[APP_CONFIG_MQTT_HOST_MAXLEN + 16];
    snprintf(uri, sizeof(uri), "mqtt://%s:%u", s_cfg.mqtt_host, s_cfg.mqtt_port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.username = s_cfg.mqtt_user[0] != '\0' ? s_cfg.mqtt_user : NULL,
        .credentials.authentication.password = s_cfg.mqtt_pass[0] != '\0' ? s_cfg.mqtt_pass : NULL,
    };

    /* register_event() voi ESP_EVENT_ANY_ID: dang ky 1 lan duy nhat cho TAT CA
     * loai su kien MQTT (CONNECTED/DISCONNECTED/DATA/ERROR/...), tat ca deu
     * chay vao chung 1 ham mqtt_event_handler() o tren, tu phan biet nhau qua
     * tham so event_id trong switch. */
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
    ESP_LOGI(TAG, "MQTT client starting, connecting to %s", uri);
}

/* wifi_manager khong biet gi ve MQTT - tu dang ky rieng 1 handler cho
 * IP_EVENT_STA_GOT_IP de biet luc nao co IP ma bat dau MQTT (giu 2 component
 * doc lap, giong pattern trong esp/ota). */
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        mqtt_start();
    }
}

static esp_err_t wifi_mqtt_start(const app_config_t *cfg)
{
    /* Sao chep NGAY DAU HAM, TRUOC khi lam bat ky viec gi khac - tham so "cfg"
     * chi la con tro tam thoi main.c truyen vao (thuong tro toi 1 bien local
     * trong app_main()), khong dam bao con song sau khi ham nay return. Toan
     * bo phan con lai cua file (mqtt_start() goi tu ip_event_handler(), chay
     * SAU khi ham nay da return tu lau) chi duoc phep doc s_cfg (ban sao),
     * KHONG bao gio duoc dung lai "cfg" (tham so goc). */
    s_cfg = *cfg;

    /* Goi CANG SOM CANG TOT, TRUOC ca wifi_manager_start() - de dong ho tinh
     * du thoi gian cho ca qua trinh ket noi WiFi (co the mat toi da 5 lan
     * retry) lan MQTT connect. Ham nay tu kiem tra co dang PENDING_VERIFY hay
     * khong, khong lam gi neu day khong phai lan boot dau sau OTA. */
    ota_rollback_start_confirm_watchdog(ROLLBACK_CONFIRM_TIMEOUT_MS);

    if (cfg->net_mode == APP_NET_STATIC) {
        wifi_static_ip_config_t static_ip = {
            .ip      = cfg->net_ip,
            .netmask = cfg->net_netmask,
            .gateway = cfg->net_gateway,
            .dns1    = cfg->net_dns1,
            .dns2    = cfg->net_dns2,
        };
        wifi_manager_start(cfg->wifi_ssid, cfg->wifi_pass, &static_ip);
    } else {
        wifi_manager_start(cfg->wifi_ssid, cfg->wifi_pass, NULL);   /* DHCP */
    }

    return esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);
}

/* ===================== Bang publish theo loai ban tin ===================== */
/*
 * Moi loai ban tin = 1 ham encode NHO, DOC LAP, chi lo dung 1 viec "dien
 * payload JSON cho loai nay". Them/sua 1 loai ban tin chi dung tim + sua/them
 * DUNG 1 ham encode_xxx() + 1 dong trong s_publish_routes[] - khong con phai
 * doc/sua 1 khoi switch chung dang phinh to dan.
 */
typedef void (*mqtt_payload_encoder_t)(const app_message_t *msg, char *payload, size_t payload_cap);

static void encode_sensor_payload(const app_message_t *msg, char *payload, size_t payload_cap)
{
    snprintf(payload, payload_cap, "{\"sensor\":%.2f}", msg->data.sensor_value);
}

static void encode_keepalive_payload(const app_message_t *msg, char *payload, size_t payload_cap)
{
    (void)msg;   /* khong dung du lieu gi tu msg, chi de tham so dong nhat voi cac encoder khac */
    snprintf(payload, payload_cap, "{\"status\":\"alive\"}");
}

static void encode_sleep_payload(const app_message_t *msg, char *payload, size_t payload_cap)
{
    snprintf(payload, payload_cap, "{\"status\":\"going_to_sleep\",\"next_wake_ms\":%" PRIu32 "}",
             msg->data.sleep_interval_ms);
}

typedef struct {
    app_msg_type_t type;
    const char *topic;
    mqtt_payload_encoder_t encode;
} mqtt_publish_route_t;

static const mqtt_publish_route_t s_publish_routes[] = {
    { APP_MSG_SENSOR,         SENSOR_TOPIC,    encode_sensor_payload },
    { APP_MSG_KEEPALIVE,      KEEPALIVE_TOPIC, encode_keepalive_payload },
    { APP_MSG_GOING_TO_SLEEP, SLEEP_TOPIC,     encode_sleep_payload },
    /* Them loai ban tin moi: { APP_MSG_XXX, TOPIC_XXX, encode_xxx_payload }, */
};
#define PUBLISH_ROUTE_COUNT (sizeof(s_publish_routes) / sizeof(s_publish_routes[0]))

static esp_err_t wifi_mqtt_publish(const app_message_t *msg)
{
    /* Chi kiem tra client DA DUOC KHOI TAO (khac NULL), KHONG kiem tra da
     * CONNECTED hay chua (xem s_mqtt_connected/wifi_mqtt_is_ready() - 2 khai
     * niem khac nhau). Neu goi publish() luc client ton tai nhung chua/khong
     * con connected, esp_mqtt_client_publish() ben duoi van nhan va tu xep
     * vao outbox noi bo cua esp-mqtt de gui khi ket noi lai duoc - khong loi
     * ngay lap tuc, nhung ban tin co the bi tre hoac mat neu thiet bi ngu lai
     * (esp_deep_sleep_start()) truoc khi outbox kip gui that qua mang. */
    if (s_mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < PUBLISH_ROUTE_COUNT; i++) {
        if (s_publish_routes[i].type != msg->type) {
            continue;
        }
        char payload[64];   /* du cho moi payload JSON hien co (dai nhat: encode_sleep_payload, ~55 ky tu) - tang len neu them loai ban tin JSON dai hon */
        s_publish_routes[i].encode(msg, payload, sizeof(payload));
        /* Tham so: topic, data, len=0 (bao esp-mqtt tu tinh bang strlen(data)),
         * qos=1 (it nhat gui duoc 1 lan, co the trung lap - du dung cho
         * telemetry khong quan trong tuyet doi thu tu/khong trung), retain=0
         * (broker KHONG luu lai ban tin nay cho subscriber moi sau nay). */
        int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_publish_routes[i].topic, payload, 0, 1, 0);
        return msg_id >= 0 ? ESP_OK : ESP_FAIL;
    }

    ESP_LOGW(TAG, "publish(): app_msg_type_t %d chua duoc dang ky trong s_publish_routes[]", msg->type);
    return ESP_ERR_NOT_SUPPORTED;
}

static bool wifi_mqtt_is_ready(void)
{
    return s_mqtt_connected;
}

const transport_if_t g_transport_wifi_mqtt = {
    .start          = wifi_mqtt_start,
    .publish        = wifi_mqtt_publish,
    .is_ready       = wifi_mqtt_is_ready,
};
