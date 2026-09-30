#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Confirm the currently running app as valid (cancel rollback) if it is
 * pending verification. Safe to call multiple times; only acts once.
 * Call at a health checkpoint (e.g. right after WiFi + MQTT connect). */
void ota_rollback_confirm_if_pending(void);

/* "Dong ho canh bao" doc lap voi Task Watchdog - neu sau timeout_ms ke tu luc
 * goi ham nay ma van CHUA xac nhan duoc rollback (con dang
 * ESP_OTA_IMG_PENDING_VERIFY), CHU DONG rollback ve firmware cu + reboot ngay
 * (khong cho 1 lan reboot ngau nhien nao khac xay ra). Xu ly dung truong hop
 * firmware moi khong crash nhung cung khong bao gio ket noi duoc WiFi/MQTT de
 * confirm - neu khong co dong ho nay, thiet bi se treo o trang thai chua-
 * xac-nhan-chua-rollback vo thoi han cho toi khi co ai rut nguon thu cong.
 *
 * An toan goi luc nao cung duoc (ke ca khi khong dang PENDING_VERIFY - tu
 * kiem tra va khong lam gi neu khong can, khong tao timer thua). Timer tu
 * huy neu ota_rollback_confirm_if_pending() confirm thanh cong truoc khi het
 * timeout_ms. Nen goi CANG SOM CANG TOT luc boot (truoc khi bat dau ket noi
 * WiFi), de dong ho tinh du ca thoi gian cho WiFi lan MQTT connect. */
void ota_rollback_start_confirm_watchdog(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
