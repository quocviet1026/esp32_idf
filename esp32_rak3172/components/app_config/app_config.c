/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* app_config.c
 *
 * Luu/doc 1 struct app_config_t duy nhat trong NVS (namespace "app_cfg", key
 * "cfg_blob") theo dang blob - moi lan CLI sua 1 truong deu load() toan bo,
 * sua field can sua, roi save() lai toan bo. Khong giu cache trong RAM: CLI
 * chay don luong (1 REPL task), tan suat ghi thap (nguoi dung go lenh), nen
 * load-mutate-save moi lan la du don gian va khong co race condition can lo.
 */

#include <string.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app_config.h"

static const char *TAG = "app_config";

#define NVS_NAMESPACE "app_cfg"
#define NVS_KEY       "cfg_blob"

/* Tinh CRC32 tren toan bo struct TRU truong crc32 (nam cuoi cung) - cach lam
 * chuan de tu-kiem-tra du lieu doc tu NVS co bi hong/ghi do (torn write) hay
 * khong, doc lap voi co che kiem tra rieng cua NVS (NVS da co checksum noi bo,
 * day la lop kiem tra THEM o muc struct, phong truong hop nvs_get_blob() tra
 * ve du lieu tu 1 phien ban config cu/khac layout ma van "hop le" voi NVS). */
static uint32_t compute_crc32(const app_config_t *cfg)
{
    return esp_rom_crc32_le(0, (const uint8_t *)cfg, offsetof(app_config_t, crc32));
}

static void fill_defaults(app_config_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic   = APP_CONFIG_MAGIC;
    out->version = APP_CONFIG_VERSION;
    out->mode    = APP_MODE_WIFI_MQTT;   /* mac dinh an toan: WiFi/MQTT bao loi ro rang hon LoRaWAN thieu key */
    out->mqtt_port = 1883;
    /* out->net_mode = APP_NET_DHCP (=0) da dung nho memset() o tren - DHCP la mac dinh an toan (khong can biet gateway/subnet mang truoc) */
    /* out->sleep_enabled = false da dung nho memset() o tren - mac dinh KHONG sleep, giu hanh vi luon-bat hien tai */
    out->sleep_interval_ms = 60000;   /* PHAI dat rieng, khac 0 - neu de memset()=0 thi esp_sleep_enable_timer_wakeup(0) se thuc lai ngay lap tuc */
}

esp_err_t app_config_load(app_config_t *out)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS namespace '%s' chua ton tai, dung config mac dinh", NVS_NAMESPACE);
        fill_defaults(out);
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        fill_defaults(out);
        return err;
    }

    app_config_t tmp;
    size_t len = sizeof(tmp);
    err = nvs_get_blob(handle, NVS_KEY, &tmp, &len);
    nvs_close(handle);

    if (err != ESP_OK || len != sizeof(tmp)) {
        ESP_LOGW(TAG, "Chua co config da luu (%s), dung config mac dinh",
                 err == ESP_OK ? "sai kich thuoc" : esp_err_to_name(err));
        fill_defaults(out);
        return ESP_ERR_NVS_NOT_FOUND;
    }

    if (tmp.magic != APP_CONFIG_MAGIC || tmp.version != APP_CONFIG_VERSION) {
        ESP_LOGW(TAG, "Config trong NVS sai magic/version (co the tu ban firmware cu khac layout struct), dung config mac dinh");
        fill_defaults(out);
        return ESP_ERR_NVS_NOT_FOUND;
    }

    if (compute_crc32(&tmp) != tmp.crc32) {
        ESP_LOGE(TAG, "Config trong NVS sai CRC32 (du lieu hong), dung config mac dinh");
        fill_defaults(out);
        return ESP_ERR_NVS_NOT_FOUND;
    }

    *out = tmp;
    return ESP_OK;
}

esp_err_t app_config_save(const app_config_t *cfg)
{
    app_config_t tmp = *cfg;
    tmp.magic   = APP_CONFIG_MAGIC;
    tmp.version = APP_CONFIG_VERSION;
    tmp.crc32   = compute_crc32(&tmp);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (RW) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY, &tmp, sizeof(tmp));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Save config failed: %s", esp_err_to_name(err));
    }
    return err;
}
