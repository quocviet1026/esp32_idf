/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* ota_manager.c
 *
 * "Cong vao" cua toan bo tinh nang OTA: nhan bai tin MQTT tren OTA_COMMAND_TOPIC,
 * kiem tra co nen OTA khong (dang chay do? version co moi hon khong?), va neu
 * hop le thi giao viec thuc su (tai + ghi firmware) cho ota_task.
 *
 * Khong tu lam gi voi ket noi HTTPS hay flash - chi dong vai tro "gac cong"
 * (guard) truoc khi trigger ota_task_start().
 */

#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "cJSON.h"

#include "ota_manager.h"
#include "ota_task.h"

static const char *TAG = "ota_manager";

static esp_mqtt_client_handle_t s_mqtt_client;
static volatile bool s_ota_in_progress = false;   /* volatile: doc/ghi tu ca mqtt_event_handler (task lwip) va ota_task */

/* So sanh version trong lenh MQTT voi version cua firmware DANG CHAY.
 *
 * LUU Y GIOI HAN: day chi la strcmp() - bat ky chuoi nao KHAC voi version
 * hien tai deu duoc coi la "moi hon" (kho ca truong hop lui version, vd tu
 * "1.2.0" ve "1.1.0" van bi coi la hop le). Neu can so sanh semver chuan
 * (1.10.0 > 1.9.0) thi phai tu viet ham parse major.minor.patch rieng. */
static bool is_version_newer(const char *incoming_version)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_desc;
    if (esp_ota_get_partition_description(running, &running_desc) != ESP_OK) {
        /* Khong doc duoc mo ta partition dang chay (khong nen xay ra trong
         * dieu kien binh thuong) - an toan la tu choi OTA thay vi lieu linh cho qua. */
        ESP_LOGE(TAG, "Failed to read running partition description, refusing OTA");
        return false;
    }

    ESP_LOGI(TAG, "Version check: running=%s, incoming=%s", running_desc.version, incoming_version);
    return strcmp(incoming_version, running_desc.version) != 0;
}

void ota_manager_start(esp_mqtt_client_handle_t client)
{
    s_mqtt_client = client;
    esp_mqtt_client_subscribe(client, OTA_COMMAND_TOPIC, 1);
    ESP_LOGI(TAG, "Subscribed to OTA command topic: %s", OTA_COMMAND_TOPIC);
}

void ota_manager_handle_mqtt_data(const esp_mqtt_event_handle_t event)
{
    /* event->topic KHONG ket thuc bang '\0' nen phai so sanh bang do dai +
     * strncmp, khong the dung strcmp() truc tiep. */
    if (event->topic_len != (int)strlen(OTA_COMMAND_TOPIC) ||
        strncmp(event->topic, OTA_COMMAND_TOPIC, event->topic_len) != 0) {
        return;   /* Khong phai topic lenh OTA - khong lien quan, bo qua trong im lang */
    }

    char payload[256] = {0};
    if (event->data_len >= (int)sizeof(payload)) {
        /* Payload bi cat bot - canh bao vi cJSON_Parse ben duoi rat co the se
         * that bai do JSON bi cat dang do (thieu dau '}' ket thuc). */
        ESP_LOGW(TAG, "OTA command payload (%d bytes) vuot qua buffer %d bytes, se bi cat bot",
                 event->data_len, (int)sizeof(payload) - 1);
    }
    int len = event->data_len < (int)sizeof(payload) - 1 ? event->data_len : (int)sizeof(payload) - 1;
    memcpy(payload, event->data, len);

    ESP_LOGI(TAG, "OTA command received: %s", payload);

    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) {
        ESP_LOGE(TAG, "Invalid OTA command JSON: %s", payload);
        return;
    }

    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *url = cJSON_GetObjectItem(root, "url");

    if (!cJSON_IsString(version) || !cJSON_IsString(url)) {
        ESP_LOGE(TAG, "OTA command missing 'version' or 'url' string field");
        cJSON_Delete(root);
        return;
    }

    /* 2 lop guard truoc khi thuc su trigger OTA:
     *   1. Dang co 1 lot OTA khac chay do (s_ota_in_progress) -> bo qua lenh moi.
     *   2. Version gui len khong "moi hon" version dang chay -> bo qua, tranh
     *      vong lap tai-lai-cung-1-file moi lan nhan trung lenh. */
    /* Cong tac an toan: mac dinh (CONFIG_OTA_MANAGER_ALLOW_HTTP=n) tu choi
     * ngay tu day bat ky URL nao khong phai https:// - khong de lot xuong
     * ota_task moi phat hien ra. Xem giai thich day du trong Kconfig cua
     * component nay (menuconfig -> Component config -> OTA Manager). */
#if !CONFIG_OTA_MANAGER_ALLOW_HTTP
    if (strncmp(url->valuestring, "https://", 8) != 0) {
        ESP_LOGE(TAG, "Refusing OTA: url is not https:// (%s) and CONFIG_OTA_MANAGER_ALLOW_HTTP is disabled",
                 url->valuestring);
        cJSON_Delete(root);
        return;
    }
#endif

    if (s_ota_in_progress) {
        ESP_LOGW(TAG, "OTA already in progress, ignoring command");
    } else if (!is_version_newer(version->valuestring)) {
        ESP_LOGI(TAG, "Version %s is not newer than running firmware, ignoring", version->valuestring);
    } else {
        ESP_LOGI(TAG, "Starting OTA to version %s from %s", version->valuestring, url->valuestring);
        s_ota_in_progress = true;
        ota_task_start(s_mqtt_client, url->valuestring);
    }

    cJSON_Delete(root);
}

void ota_manager_notify_task_done(void)
{
    ESP_LOGI(TAG, "OTA task finished without rebooting, ready to accept new OTA commands");
    s_ota_in_progress = false;
}
