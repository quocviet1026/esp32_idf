/* ota_rollback.c
 *
 * Xac nhan firmware DANG CHAY la hop le, huy tien trinh App Rollback tu dong
 * cua bootloader. Chi co y nghia khi CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
 * duoc bat (menuconfig -> Bootloader config -> Enable app rollback support).
 *
 * Nhac lai co che (xem docs/OTA_PLAN.md muc "App Rollback" de biet chi tiet):
 *   OTA thanh cong -> state = NEW -> reboot -> bootloader tu chuyen state
 *   thanh PENDING_VERIFY -> app moi PHAI tu goi 1 trong 2 ham:
 *     - esp_ota_mark_app_valid_cancel_rollback()          -> state = VALID
 *     - esp_ota_mark_app_invalid_rollback_and_reboot()    -> rollback ve app cu
 *   Neu reboot lai ma van con PENDING_VERIFY (khong ai goi ham nao ca) thi
 *   bootloader tu dong coi la ABORTED va rollback - day la "luoi bao ve ngam
 *   dinh" ngay ca khi code quen khong goi ham confirm.
 *
 * File nay chon "checkpoint" xac nhan la: ket noi duoc WiFi + MQTT thanh cong
 * (goi tu main/ota.c ngay sau MQTT_EVENT_CONNECTED lan dau) - khong dung GPIO
 * vat ly nhu vi du chinh thuc native_ota_example.
 */

#include <stdbool.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

#include "ota_rollback.h"

static const char *TAG = "ota_rollback";
static bool s_checked = false;   /* Dam bao logic ben duoi chi chay dung 1 lan trong ca vong doi app_main() */
static esp_timer_handle_t s_watchdog_timer = NULL;   /* NULL = chua tao, hoac da huy sau khi confirm thanh cong */

/* Huy dong ho canh bao (neu dang chay) - goi khi da confirm thanh cong, vi
 * luc do khong con gi de "canh bao" nua. esp_timer_stop() an toan goi ke ca
 * khi timer (one-shot) da tu dung roi (khong loi, chi tra ve ESP_ERR_INVALID_STATE
 * bi bo qua o day). */
static void stop_watchdog_if_running(void)
{
    if (s_watchdog_timer != NULL) {
        esp_timer_stop(s_watchdog_timer);
        esp_timer_delete(s_watchdog_timer);
        s_watchdog_timer = NULL;
    }
}

void ota_rollback_confirm_if_pending(void)
{
    if (s_checked) {
        return;   /* Da xu ly roi (vd MQTT reconnect nhieu lan trong 1 phien chay) - khong lam lai */
    }
    s_checked = true;

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        /* Binh thuong, KHONG phai loi: partition dang chay khong phai la 1
         * trong 2 OTA slot (ota_0/ota_1) - vd dang chay "factory" (truong hop
         * thuong gap sau khi flash tay qua USB lan dau, vi otadata con trong
         * nen bootloader mac dinh boot vao factory). Rollback state chi ton
         * tai cho OTA slot, factory khong bao gio can confirm gi ca. */
        ESP_LOGI(TAG, "Running partition \"%s\" is not an OTA slot (factory/test), nothing to confirm", running->label);
        return;
    }
    if (err != ESP_OK) {
        /* Loi that su (vd otadata bi hong/khong doc duoc) - can chu y. */
        ESP_LOGE(TAG, "Failed to read running partition OTA state: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Running partition \"%s\", OTA state = %d", running->label, ota_state);

    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        /* Day chinh la lan boot dau tien sau 1 lot OTA - can confirm ngay,
         * cang som cang tot, de tranh truong hop crash/mat dien truoc khi
         * kip confirm (luc do bootloader se tu rollback o lan boot ke tiep). */
        ESP_LOGI(TAG, "Firmware is pending verification, confirming as valid...");
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "App confirmed valid, rollback cancelled");
            stop_watchdog_if_running();   /* da confirm xong - khong con gi de "dong ho" canh bao nua */
        } else {
            ESP_LOGE(TAG, "Failed to cancel rollback");
        }
    } else {
        /* Cac state khac (VALID, UNDEFINED...) nghia la khong phai lan boot
         * dau sau OTA - khong can lam gi, chi log lai de tien theo doi. */
        ESP_LOGD(TAG, "No rollback confirmation needed (state=%d)", ota_state);
    }
#else
    ESP_LOGD(TAG, "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is off, nothing to confirm");
#endif
}

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
/* Chay trong context cua esp_timer task (task he thong rieng, KHONG phai
 * task goi ota_rollback_start_confirm_watchdog()) khi timer het han. Tu kiem
 * tra lai state TRUOC khi hanh dong - phong truong hop hiem gap: confirm vua
 * xay ra dung luc timer sap no (stop_watchdog_if_running() chua kip huy). */
static void watchdog_timeout_cb(void *arg)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGE(TAG, "KHONG xac nhan duoc rollback dung han - CHU DONG rollback ve firmware cu va reboot ngay");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        /* Ham tren KHONG BAO GIO return neu thanh cong - no tu esp_restart()
         * ben trong. Dong log duoi day CHI chay toi neu ban than ham do that
         * bai (rat hiem, vd loi doc/ghi flash). */
        ESP_LOGE(TAG, "esp_ota_mark_app_invalid_rollback_and_reboot() that bai bat thuong");
    }
    /* else: da confirm roi hoac khong con la OTA slot dang PENDING_VERIFY -
     * khong lam gi, im lang thoat (truong hop nay hiem xay ra vi
     * stop_watchdog_if_running() thuong da huy timer truoc khi no kip no). */
}
#endif

void ota_rollback_start_confirm_watchdog(uint32_t timeout_ms)
{
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err != ESP_OK || ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
        /* Khong phai lan boot dau sau OTA (hoac dang chay tu factory/khong
         * phai OTA slot) - khong co gi de canh bao, khong tao timer thua. */
        return;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = &watchdog_timeout_cb,
        .name = "ota_rb_wdt",
    };
    err = esp_timer_create(&timer_args, &s_watchdog_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong tao duoc dong ho canh bao rollback: %s", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK(esp_timer_start_once(s_watchdog_timer, (uint64_t)timeout_ms * 1000ULL));
    ESP_LOGI(TAG, "Da bat dong ho canh bao rollback: %" PRIu32 " ms - se CHU DONG rollback neu chua xac nhan dung han",
             timeout_ms);
#else
    (void)timeout_ms;
#endif
}
