/* main/main.c
 *
 * Entry point cua firmware: thiet lap ha tang nen (goi WiFi.c de ket noi
 * WiFi STA, roi tu khoi tao 1 MQTT client toi broker.hivemq.com ngay khi co
 * IP) roi giao viec cho 2 module chuyen trach:
 *   - ota_manager (components/ota_manager): nhan lenh OTA qua MQTT, quan ly
 *     toan bo qua trinh tai + ghi firmware moi.
 *   - ota_rollback (components/ota_manager): xac nhan firmware dang chay la
 *     hop le (huy Rollback) ngay sau khi co "checkpoint" dau tien (o day chon
 *     la: WiFi + MQTT ket noi thanh cong).
 *
 * File nay KHONG tu xu ly logic WiFi (xem WiFi.c) hay logic OTA - chi lam
 * nhiem vu "day event qua" cho cac module tren tai dung diem trong vong doi
 * ket noi.
 */

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "inttypes.h"

#include "WiFi.h"
#include "ota_manager.h"
#include "ota_rollback.h"

#define MQTT_BROKER_URI "mqtt://broker.hivemq.com:1883"   /* Plain MQTT (khong TLS) - broker cong cong, cho phep ket noi an danh */

/* Topic "test" chung - chi de demo/log ra man hinh, khong gan voi logic OTA */
#define MQTT_TOPIC_1 "esp32/vietnq/topic1"
#define MQTT_TOPIC_2 "esp32/vietnq/topic2"
/* Topic thiet bi tu cong bo version firmware dang chay, bat moi lan MQTT connect thanh cong */
#define VERSION_TOPIC "esp32/vietnq/version"

/* Neu sau tung nay ms ke tu luc bat dau ket noi WiFi ma van CHUA confirm
 * duoc rollback (vd WiFi/MQTT khong bao gio ket noi duoc du khong crash),
 * ota_rollback se CHU DONG rollback ve firmware cu thay vi treo vo thoi han -
 * xem ota_rollback_start_confirm_watchdog() va "Sự cố đã gặp" trong
 * docs/OTA_PLAN.md. 2 phut du rong rai cho ca WiFi retry (toi da 5 lan, xem
 * WiFi.c) lan MQTT connect trong dieu kien mang binh thuong. */
#define ROLLBACK_CONFIRM_TIMEOUT_MS (2 * 60 * 1000)

static const char *TAG = "main";

static const char *s_topic_list[] = {
    MQTT_TOPIC_1,
    MQTT_TOPIC_2,
};
#define TOPIC_LIST_SIZE (sizeof(s_topic_list) / sizeof(s_topic_list[0]))

static void mqtt_start(void);

/* WiFi.c chi lo ket noi mang, khong biet gi ve MQTT - nen o day tu dang ky
 * rieng 1 handler cho IP_EVENT_STA_GOT_IP de biet luc nao co IP ma bat dau
 * MQTT, thay vi sua WiFi.c de goi nguoc lai ham cua main.c (giu 2 file doc
 * lap, khong phu thuoc nhau). */
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        mqtt_start();   /* Chi bat dau ket noi MQTT sau khi da chac chan co IP */
    }
}

/* Callback duy nhat cho toan bo vong doi MQTT client. esp-mqtt goi lai ham nay
 * cho MOI event (connect, disconnect, subscribe ack, nhan du lieu, loi...) -
 * phan biet bang event_id trong switch ben duoi. */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected successfully");

        /* Cong bo version firmware dang chay ngay khi vua ket noi - giup ai
         * dang theo doi qua MQTT (mosquitto_sub) biet ngay thiet bi nao dang
         * chay ban nao ma khong can doi toi luc co OTA. */
        {
            const esp_app_desc_t *app_desc = esp_app_get_description();
            char version_payload[64];
            snprintf(version_payload, sizeof(version_payload), "{\"version\":\"%s\"}", app_desc->version);
            esp_mqtt_client_publish(client, VERSION_TOPIC, version_payload, 0, 1, 0);
            ESP_LOGI(TAG, "Published %s to %s", version_payload, VERSION_TOPIC);
        }

        /* Subscribe cac topic "test" chung - khong lien quan OTA */
        for (size_t i = 0; i < TOPIC_LIST_SIZE; i++) {
            esp_mqtt_client_subscribe(client, s_topic_list[i], 0);
            ESP_LOGI(TAG, "Subscribed to topic: %s", s_topic_list[i]);
        }

        /* Giao client cho ota_manager de no tu subscribe topic lenh OTA rieng
         * va co the publish trang thai OTA sau nay. */
        ota_manager_start(client);

        /* Checkpoint xac nhan Rollback: coi "WiFi + MQTT ket noi duoc" la bang
         * chung firmware moi hoat dong tot. Ham nay tu bo qua neu da goi 1 lan
         * roi (vd MQTT bi disconnect/reconnect nhieu lan trong 1 phien chay). */
        ota_rollback_confirm_if_pending();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected, esp-mqtt se tu dong thu ket noi lai");
        break;

    case MQTT_EVENT_DATA:
        /* event->topic/event->data KHONG ket thuc bang '\0' - luon phai dung
         * kem event->topic_len/data_len (nhu %.*s duoi day), khong duoc goi
         * strlen()/strcmp() truc tiep tren chung. */
        ESP_LOGI(TAG, "Received message on topic [%.*s]: %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        /* Chuyen tiep MOI bai tin cho ota_manager - ham nay tu kiem tra topic
         * co phai la topic lenh OTA khong, khong phai thi tu bo qua. */
        ota_manager_handle_mqtt_data(event);
        break;

    case MQTT_EVENT_ERROR:
        /* Log chi tiet nguyen nhan loi thay vi chi in "MQTT error" chung chung -
         * error_handle chi hop le/co y nghia tuong ung voi tung error_type. */
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

static void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);   /* Bat dau ket noi bat dong bo - ket qua se ve qua mqtt_event_handler */
    ESP_LOGI(TAG, "MQTT client starting, connecting to %s", MQTT_BROKER_URI);
}

/* Duyet toan bo partition table (doc truc tiep tu flash, khong phu thuoc NVS
 * hay WiFi/MQTT) va log ra tung partition - huu ich de xac nhan ngay luc boot
 * layout thuc te tren board co dung nhu ky vong khong (vd co du 3 app
 * partition factory/ota_0/ota_1 sau khi doi sang Two OTA definitions chua),
 * va biet dang chay tu partition nao (danh dau "<-- RUNNING"). */
static void log_partition_table(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();

    ESP_LOGI(TAG, "Partition table:");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        const char *type_str = (p->type == ESP_PARTITION_TYPE_APP) ? "app " : "data";
        bool is_running = (running != NULL) && (p->address == running->address);
        ESP_LOGI(TAG, "  %-10s | type=%s subtype=0x%02x | addr=0x%08" PRIx32 " size=%" PRIu32 " KB%s",
                 p->label, type_str, p->subtype, p->address, p->size / 1024,
                 is_running ? "  <-- RUNNING" : "");
    }
    esp_partition_iterator_release(it);
}

void app_main(void)
{
    log_partition_table();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition doi dinh dang hoac het cho (thuong gap sau khi doi
         * partition table, vd tu single-app sang two-OTA) - xoa va tao lai. */
        ESP_LOGW(TAG, "NVS needs erase (%s), reinitializing...", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Goi CANG SOM CANG TOT, TRUOC ca wifi_manager_start() - de dong ho tinh
     * du thoi gian cho ca qua trinh ket noi WiFi (co the mat toi da 5 lan
     * retry) lan MQTT connect. Ham nay tu kiem tra co dang PENDING_VERIFY hay
     * khong, khong lam gi neu day khong phai lan boot dau sau OTA. */
    ota_rollback_start_confirm_watchdog(ROLLBACK_CONFIRM_TIMEOUT_MS);

    wifi_manager_start();
    /* wifi_manager_start() da tao xong event loop mac dinh va bat dau ket noi -
     * dang ky them handler rieng o day de biet luc nao co IP (an toan, khong
     * co race: WIFI_EVENT/IP_EVENT chi thuc su ban ra sau khi esp_wifi_start()
     * ben trong wifi_manager_start() da return va scheduler chay task event loop). */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    /* app_main() ket thuc o day - phan con lai cua chuong trinh hoan toan chay
     * theo event (WiFi/IP/MQTT/OTA), khong co vong lap while(1) nao trong task nay. */
}
