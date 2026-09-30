/* wifi_manager.c
 *
 * Toan bo logic ket noi WiFi station: cau hinh SSID/password, khoi tao netif
 * + event loop mac dinh, va tu dong retry khi mat ket noi. Component nay
 * khong biet gi ve MQTT/OTA - ai can biet "da co IP chua" thi tu dang ky
 * them handler rieng cho IP_EVENT_STA_GOT_IP (xem main/main.c), khong sua o day.
 *
 * LUU Y BAO MAT: WIFI_SSID/WIFI_PASS dang hardcode plaintext o day - chap
 * nhan duoc cho 1 board dev/test, nhung neu dung cho nhieu thiet bi that
 * (production) nen chuyen sang luu trong NVS (co the cau hinh qua BLE/
 * provisioning) thay vi hardcode trong source.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "WiFi.h"

#define WIFI_SSID "Quoc Viet T3"
#define WIFI_PASS "123456a@"
#define WIFI_MAX_RETRY 5   /* Sau tung nay lan reconnect that bai lien tiep thi ngung tu dong thu lai */

static const char *TAG = "wifi_manager";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0   /* Bit bao WiFi da co IP - hien tai chi set/clear de theo doi trang thai, chua co ai cho (xWaitBits) tren bit nay */

static int s_retry_count = 0;

/* Xu ly toan bo vong doi WiFi station: tu luc driver start, mat ket noi/
 * reconnect, cho toi luc nhan duoc IP tu DHCP. Dang ky cho ca 2 event base
 * (WIFI_EVENT va IP_EVENT) vi ham nay dung chung 1 callback cho don gian. */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* Driver WiFi vua khoi tao xong (chua co ket noi) - chu dong goi connect lan dau. */
        ESP_LOGI(TAG, "Wifi station started, connecting to AP \"%s\"...", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* Log ro ma ly do mat ket noi (vd auth that bai, khong tim thay AP, AP
         * chu dong ngat...) - rat huu ich khi debug WiFi chap chon ngoai thuc
         * te, thay vi chi biet "da mat ket noi" chung chung. */
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *) event_data;
        ESP_LOGW(TAG, "Wifi disconnected, reason=%d, rssi=%d", disconnected->reason, disconnected->rssi);

        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "Retrying wifi connection (%d/%d)", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            ESP_LOGE(TAG, "Failed to connect to wifi after %d retries, giving up", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Wifi connected successfully, IP: " IPSTR ", Gateway: " IPSTR ", Netmask: " IPSTR,
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.gw), IP2STR(&event->ip_info.netmask));
        s_retry_count = 0;   /* Reset dem retry - lan mat ket noi tiep theo (neu co) duoc tinh lai tu dau */
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_manager_start(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* ESP_EVENT_ANY_ID cho WIFI_EVENT: bat het moi event WiFi (start, disconnected,
     * connected...), tu loc ben trong wifi_event_handler bang if/else theo event_id. */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());   /* Trigger WIFI_EVENT_STA_START -> wifi_event_handler tu goi esp_wifi_connect() */

    ESP_LOGI(TAG, "Connecting to wifi SSID: %s", WIFI_SSID);
}
