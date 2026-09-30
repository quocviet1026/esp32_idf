/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* ota_task.c
 *
 * Luong OTA thuc su: mo ket noi HTTPS toi server firmware, tai ve va ghi vao
 * OTA partition khong chay (Safe Update Mode), roi chuyen boot sang partition
 * do neu chu ky hop le (Secure OTA Without Secure Boot). Toan bo tien trinh
 * duoc bao cao tung buoc len MQTT qua publish_status() (xem bang day du cac
 * trang thai trong docs/OTA_PLAN.md).
 *
 * Chay trong 1 FreeRTOS task rieng (khong phai task cua MQTT hay WiFi) de
 * khong lam nghen/cham cac xu ly khac trong luc tai file co the mat vai chuc
 * giay.
 */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include "ota_task.h"
#include "ota_manager.h"

static const char *TAG = "ota_task";

/* Symbol do linker tu sinh ra tu EMBED_TXTFILES "server_certs/ca_cert.pem"
 * trong CMakeLists.txt cua component nay - "_binary_<ten_file>_<start/end>" la
 * quy uoc dat ten co dinh cua co che nay, khong phai ten mot ham/bien tu viet.
 * Day chinh la cert TU KY dung de ESP32 tin tuong dung HTTPS server local khi
 * bat tay TLS (xem "Co che ESP32 verify HTTPS server" trong docs/OTA_PLAN.md). */
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[]   asm("_binary_ca_cert_pem_end");

typedef struct {
    esp_mqtt_client_handle_t mqtt_client;
    char *url;   /* Ban sao rieng cua URL - task tu free() khi ket thuc, khong dung con tro cua caller */
} ota_task_args_t;

/* Publish 1 trang thai OTA len OTA_STATUS_TOPIC dang JSON, dong thoi log ra
 * serial console - moi noi trong file nay can bao trang thai deu goi qua ham
 * nay de dam bao MQTT va serial log luon khop nhau. */
static void publish_status(esp_mqtt_client_handle_t client, const char *status, int progress)
{
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\",\"progress\":%d}", status, progress);
    esp_mqtt_client_publish(client, OTA_STATUS_TOPIC, payload, 0, 1, 0);
    ESP_LOGI(TAG, "Status -> %s (%d)", status, progress);
}

/* Handler cho ESP_HTTPS_OTA_EVENT - thu vien esp_https_ota tu dong ban su
 * kien nay o tung moc quan trong trong luc esp_https_ota_begin()/perform()/
 * finish() chay, khong can code trong ota_task() phai tu goi publish_status()
 * cho tung buoc (tru cac truong hop dac biet khong co event tuong ung, xem
 * comment tai noi goi esp_https_ota_begin() ben duoi). */
static void ota_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)arg;
    if (event_base != ESP_HTTPS_OTA_EVENT) {
        return;
    }

    switch (event_id) {
    case ESP_HTTPS_OTA_START:
        publish_status(client, "OTA_START", 0);
        break;
    case ESP_HTTPS_OTA_CONNECTED:
        /* Chi ban ra khi ket noi TCP/TLS toi HTTPS server THANH CONG - xem
         * nhanh "if (err != ESP_OK)" ngay sau esp_https_ota_begin() ben duoi
         * de biet truong hop THAT BAI duoc bao cao the nao (khong co event). */
        publish_status(client, "CONNECTED", 0);
        break;
    case ESP_HTTPS_OTA_GET_IMG_DESC:
        publish_status(client, "READING_IMG_DESC", 0);
        break;
    case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
        publish_status(client, "VERIFYING_CHIP_ID", 0);
        break;
    case ESP_HTTPS_OTA_VERIFY_CHIP_REVISION:
        publish_status(client, "VERIFYING_CHIP_REVISION", 0);
        break;
    case ESP_HTTPS_OTA_WRITE_FLASH:
        /* event_data la con tro toi so byte da ghi TICH LUY tinh tu dau -
         * event nay ban ra lien tuc trong suot qua trinh tai, nen day la
         * trang thai xuat hien NHIEU LAN NHAT trong ca lot OTA. */
        publish_status(client, "WRITING_FLASH", *(int *)event_data);
        break;
    case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
        /* Toi day nghia la chu ky da duoc verify OK (Secure OTA), chuan bi
         * chuyen otadata sang tro toi partition vua ghi. */
        publish_status(client, "BOOT_PARTITION_UPDATED", 100);
        break;
    case ESP_HTTPS_OTA_FINISH:
        publish_status(client, "OTA_FINISH", 100);
        break;
    case ESP_HTTPS_OTA_ABORT:
        publish_status(client, "OTA_ABORT", -1);
        break;
    default:
        break;
    }
}

static void ota_task(void *pvParameter)
{
    ota_task_args_t *args = (ota_task_args_t *)pvParameter;
    esp_mqtt_client_handle_t mqtt_client = args->mqtt_client;
    char *url = args->url;
    free(args);   /* Chi free struct wrapper - "url" ben trong van con dung, se tu free o cac nhanh thoat ben duoi */

    ESP_LOGI(TAG, "OTA task started, free heap = %lu bytes, target url = %s",
             (unsigned long)esp_get_free_heap_size(), url);

    /* Dang ky handler NGAY TRUOC khi bat dau, huy dang ky (unregister) o MOI
     * nhanh thoat cua ham nay (ke ca nhanh thanh cong truoc esp_restart()) -
     * neu khong, lan OTA sau se dang ky trung, khien 1 event ban ra 2 lan. */
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &ota_event_handler, mqtt_client));

    /* Scheme (http/https) doc thang tu URL nhan duoc, KHONG hardcode - de doi
     * giua HTTP/HTTPS chi can doi URL trong lenh MQTT, khong can sua code/build
     * lai. cert_pem chi gan khi la https:// (server http:// khong co TLS nen
     * khong co gi de verify). ota_manager.c da chan tu truoc moi URL khong phai
     * https:// tru khi CONFIG_OTA_MANAGER_ALLOW_HTTP duoc bat rat ro rang. */
    bool is_https = (strncmp(url, "https://", 8) == 0);
    esp_http_client_config_t http_config = {
        .url = url,
        .cert_pem = is_https ? (char *)server_cert_pem_start : NULL,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
        .buffer_size = 4096,   /* Tuning Performance: buffer doc HTTP lon hon mac dinh -> it round-trip mang hon */
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .bulk_flash_erase = true,   /* Tuning Performance: erase toan bo partition dich 1 lan thay vi tung block 4K luc ghi - nhanh hon */
        /* Tuning Performance: ep buffer OTA nam trong internal RAM (nhanh hon PSRAM
         * neu board co PSRAM).
         *
         * QUAN TRONG - bug tung gap: chi dung MALLOC_CAP_INTERNAL mot minh se gay
         * Guru Meditation Error (LoadStoreError) tren board that, crash ngay trong
         * esp_ota_verify_chip_id() luc esp_https_ota doc 1 truong 2-byte trong
         * buffer. Ly do (doc trong esp_heap_caps.h): MALLOC_CAP_INTERNAL chi nghia
         * la "vung nho noi bo", KHONG dam bao ho tro truy cap theo byte (8-bit) -
         * bo cap phat duoc phep tra ve IRAM (vung chua code thuc thi, tren Xtensa
         * chi ho tro doc/ghi 32-bit). Neu buffer OTA vo tinh nam trong IRAM, bat ky
         * phep doc/ghi le byte nao (nhu truong 2-byte chip_id trong esp_image_header_t)
         * deu gay LoadStoreError. Phai luon cong them MALLOC_CAP_8BIT de dam bao
         * chi nhan vung DRAM thuong (ho tro byte-access), khong bao gio la IRAM. */
        .buffer_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        /* esp_https_ota_begin() chi dispatch event ESP_HTTPS_OTA_CONNECTED khi
         * ket noi THANH CONG (da xac nhan bang cach doc source esp_https_ota.c) -
         * that bai o day (sai IP/port, server chua chay, TLS handshake fail do
         * cert khong khop CN...) se KHONG co event nao ban ra ca. Vi vay phai
         * tu publish_status() rieng, neu khong loi nay chi hien qua serial log. */
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        publish_status(mqtt_client, "HTTPS_CONNECT_FAILED", -1);
        goto cleanup;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        goto cleanup;
    }
    ESP_LOGI(TAG, "Downloading firmware version: %s, image size: %d bytes",
             app_desc.version, esp_https_ota_get_image_size(https_ota_handle));

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;   /* Thoat vong lap khi xong (ESP_OK) hoac loi thuc su (khac ESP_ERR_HTTPS_OTA_IN_PROGRESS) */
        }
        /* Log tien do o muc DEBUG (khong lam ngap serial log o muc INFO mac
         * dinh) - bat len bang idf.py menuconfig -> Log output -> Debug, hoac
         * esp_log_level_set(TAG, ESP_LOG_DEBUG) trong code neu can xem chi tiet. */
        ESP_LOGD(TAG, "Progress: %d/%d bytes",
                 esp_https_ota_get_image_len_read(https_ota_handle),
                 esp_https_ota_get_image_size(https_ota_handle));
    }

    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        /* err co the da la ESP_OK (server dong ket noi som) nhung du lieu
         * chua nhan du - van phai coi la loi, khong duoc finish() voi anh
         * thieu du lieu. */
        ESP_LOGE(TAG, "Incomplete OTA data received (err=%s)", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        goto cleanup;
    }

    /* esp_https_ota_finish() luon giai phong https_ota_handle du thanh cong
     * hay that bai (da doc source de xac nhan) - vi vay KHONG duoc goi thêm
     * esp_https_ota_abort() sau lenh nay trong bat ky nhanh nao, se dung double-free. */
    err = esp_https_ota_finish(https_ota_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA succeeded, rebooting...");
        publish_status(mqtt_client, "REBOOTING", 100);
        esp_event_handler_unregister(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &ota_event_handler);
        free(url);
        vTaskDelay(pdMS_TO_TICKS(1000));   /* Cho publish o tren kip gui di truoc khi mat ket noi do reboot */
        esp_restart();   /* Khong bao gio chay toi dong duoi day */
    }

    if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
        /* Day la ket qua cua Secure OTA Without Secure Boot: chu ky sai hoac
         * anh bi hong/bi sua doi giua duong - tu choi cai dat, GIU NGUYEN
         * firmware dang chay, khong reboot. */
        ESP_LOGE(TAG, "Firmware signature invalid, rejecting image");
        publish_status(mqtt_client, "SIGNATURE_INVALID", -1);
    } else {
        ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
    }

cleanup:
    /* Diem thoat chung cho MOI nhanh loi phia tren (tru nhanh thanh cong da
     * esp_restart() o tren, khong bao gio toi day). Luon phai:
     *   1. Unregister event handler - tranh dang ky trung o lan OTA sau.
     *   2. free(url) - trach nhiem cua task nay (duoc strdup() rieng trong ota_task_start()).
     *   3. ota_manager_notify_task_done() - QUAN TRONG: neu quen dong nay,
     *      s_ota_in_progress ben ota_manager.c se ket dung true mai mai, thiet
     *      bi se khong bao gio nhan lenh OTA nao nua cho toi khi tu reboot
     *      bang cach khac (vd mat dien). */
    esp_event_handler_unregister(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &ota_event_handler);
    free(url);
    ota_manager_notify_task_done();
    vTaskDelete(NULL);
}

void ota_task_start(esp_mqtt_client_handle_t mqtt_client, const char *url)
{
    ota_task_args_t *args = malloc(sizeof(ota_task_args_t));
    args->mqtt_client = mqtt_client;
    args->url = strdup(url);   /* Task tu so huu ban sao nay - an toan du "url" goc (tu cJSON) bi giai phong ngay sau khi ham nay return */
    xTaskCreate(&ota_task, "ota_task", 8192, args, 5, NULL);
    ESP_LOGI(TAG, "ota_task created (stack 8192 bytes, priority 5)");
}
