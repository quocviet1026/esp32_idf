/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* wifi_manager.c
 *
 * Adapted tu /home/vietnq/esp/ota/components/wifi_manager/WiFi.c: toan bo
 * logic ket noi WiFi station (event group, retry, log) giu nguyen - CHI KHAC
 * o cho ssid/password truyen vao qua tham so ham thay vi hardcode #define,
 * vi du nay doc tu app_config (NVS) qua CLI console thay vi co dinh trong
 * source. Component nay van khong biet gi ve MQTT/OTA - ai can biet "da co IP
 * chua" thi tu dang ky them handler rieng cho IP_EVENT_STA_GOT_IP (xem main.c).
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "wifi_manager.h"

#define WIFI_MAX_RETRY 5   /* Sau tung nay lan reconnect that bai lien tiep thi ngung tu dong thu lai */

static const char *TAG = "wifi_manager";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static int  s_retry_count = 0;
static char s_ssid_for_log[33];   /* chi de log - khop kich thuoc wifi_config_t.sta.ssid */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wifi station started, connecting to AP \"%s\"...", s_ssid_for_log);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *) event_data;
        ESP_LOGW(TAG, "Wifi disconnected, reason=%d, rssi=%d", disconnected->reason, disconnected->rssi);

        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "Retrying wifi connection (%d/%d)", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            ESP_LOGE(TAG, "Failed to connect to wifi after %d retries, giving up "
                          "(dung lenh CLI 'wifi_set' + 'reboot' de thu lai voi thong tin khac)",
                     WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Wifi connected successfully, IP: " IPSTR ", Gateway: " IPSTR ", Netmask: " IPSTR,
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.gw), IP2STR(&event->ip_info.netmask));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Dung khi static_ip != NULL: tat DHCP client roi tu dat IP/netmask/gateway/
 * DNS cho netif STA. PHAI goi truoc esp_wifi_start() - dhcpc_stop() se that
 * bai (ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) neu goi sau khi DHCP client
 * da tu khoi dong. */
static void apply_static_ip(esp_netif_t *sta_netif, const wifi_static_ip_config_t *static_ip)
{
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(sta_netif));

    esp_netif_ip_info_t ip_info = { 0 };
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(static_ip->ip, &ip_info.ip));
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(static_ip->netmask, &ip_info.netmask));
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(static_ip->gateway, &ip_info.gw));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &ip_info));

    if (static_ip->dns1 != NULL && static_ip->dns1[0] != '\0') {
        esp_netif_dns_info_t dns = { .ip.type = ESP_IPADDR_TYPE_V4 };
        ESP_ERROR_CHECK(esp_netif_str_to_ip4(static_ip->dns1, &dns.ip.u_addr.ip4));
        ESP_ERROR_CHECK(esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns));
    }
    if (static_ip->dns2 != NULL && static_ip->dns2[0] != '\0') {
        esp_netif_dns_info_t dns = { .ip.type = ESP_IPADDR_TYPE_V4 };
        ESP_ERROR_CHECK(esp_netif_str_to_ip4(static_ip->dns2, &dns.ip.u_addr.ip4));
        ESP_ERROR_CHECK(esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &dns));
    }

    ESP_LOGI(TAG, "Static IP configured: " IPSTR " / " IPSTR " / gw " IPSTR,
             IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw));
}

void wifi_manager_start(const char *ssid, const char *password, const wifi_static_ip_config_t *static_ip)
{
    strlcpy(s_ssid_for_log, ssid, sizeof(s_ssid_for_log));

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    if (static_ip != NULL) {
        apply_static_ip(sta_netif, static_ip);
    }   /* else: giu nguyen hanh vi DHCP mac dinh cua esp_netif_create_default_wifi_sta() */

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to wifi SSID: %s", s_ssid_for_log);
}
