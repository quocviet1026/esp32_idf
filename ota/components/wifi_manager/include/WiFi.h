#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Khoi tao WiFi STA (netif, event loop mac dinh, driver WiFi) va bat dau ket
 * noi toi AP cau hinh trong wifi_manager.c. Goi 1 lan duy nhat trong app_main().
 * Sau khi ham nay return, viec ket noi/retry/nhan IP chay hoan toan theo
 * event (WIFI_EVENT/IP_EVENT), khong blocking. */
void wifi_manager_start(void);

#ifdef __cplusplus
}
#endif
