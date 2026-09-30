---
name: 'C / ESP-IDF coding rules'
description: 'Quy tắc viết code C cho firmware ESP32-C5 + ESP-IDF: naming, error handling, memory, FreeRTOS, edge case, security. Dùng khi tạo hoặc sửa file .c/.h.'
applyTo: '**/*.c,**/*.h'
---

# Quy tắc code C / ESP-IDF

Mục tiêu: code **đúng, dễ đọc, tái sử dụng, chạy ổn định nhiều năm** trên thiết bị không có người trông.
Mỗi quy tắc có lý do (`Vì:`) để bạn áp dụng đúng tinh thần khi gặp tình huống chưa được mô tả.

## 1. Bố cục file

Thứ tự bắt buộc trong file `.c`:

```c
/* <ten_file>.c
 *
 * Trach nhiem cua module (1-3 cau), rang buoc threading, tai nguyen so huu.
 */

#include <string.h> /* 1. C standard */
#include <stdint.h>

#include "freertos/FreeRTOS.h" /* 2. FreeRTOS / ESP-IDF */
#include "esp_log.h"
#include "esp_check.h"

#include "transport.h" /* 3. component khac cua du an */

#include "sensor_internal.h" /* 4. header noi bo cua chinh component */

static const char *TAG = "sensor"; /* TAG = ten component */

/* macro/const -> type noi bo -> bien static -> prototype static -> ham static -> ham public */
```

Header `.h`:

```c
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* API public, moi ham co comment: lam gi, tham so, gia tri tra ve, thread-safety, context duoc goi */

#ifdef __cplusplus
}
#endif
```

- Header public chỉ đặt trong `components/<x>/include/`, chỉ chứa những gì component khác cần.
  Kiểu/hàm nội bộ để trong `<x>_internal.h` (không nằm trong `include/`). **Vì:** giảm coupling,
  đổi implementation không ảnh hưởng nơi khác.
- Header phải tự đủ (self-contained): `#include` được một mình mà không lỗi.

## 2. Đặt tên

| Loại | Quy ước | Ví dụ |
|---|---|---|
| Hàm public | `<component>_<verb>[_<object>]` | `rak3172_send_uplink()` |
| Hàm static | `snake_case`, không cần prefix | `parse_evt_line()` |
| Biến static file-scope | `s_` + snake_case | `s_cmd_lock` |
| Biến global (hạn chế tối đa) | `g_` + snake_case, chỉ trong `*_internal.h` | `g_transport_mqtt` |
| Kiểu | `snake_case_t` | `app_message_t` |
| Enum value / macro | `UPPER_CASE` có prefix component | `APP_MSG_SENSOR`, `RAK3172_LINE_MAXLEN` |
| Callback type | `<component>_<event>_cb_t` | `rak3172_join_cb_t` |
| Handle opaque | `<component>_handle_t` | `button_handle_t` |
| Kconfig | `CONFIG_<COMPONENT>_<NAME>` | `CONFIG_RAK3172_TX_GPIO` |

Đơn vị đưa vào tên biến: `timeout_ms`, `interval_s`, `len_bytes`, `temp_centi_c`.
**Vì:** lỗi nhầm đơn vị (ms với tick, byte với phần tử) là lỗi phổ biến nhất trong firmware.

## 3. Xử lý lỗi

- Hàm có thể lỗi → trả `esp_err_t`; dữ liệu trả qua con trỏ output (`out_` prefix).
- Kiểm tra tham số đầu hàm public, dùng macro của `esp_check.h`:

```c
esp_err_t sensor_read(sensor_handle_t h, sensor_sample_t *out_sample)
{
    ESP_RETURN_ON_FALSE(h != NULL && out_sample != NULL, ESP_ERR_INVALID_ARG, TAG, "null arg");
    ESP_RETURN_ON_FALSE(h->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    esp_err_t ret = ESP_OK;
    uint8_t raw[SENSOR_FRAME_LEN] = { 0 };

    ESP_GOTO_ON_ERROR(bus_lock(h, pdMS_TO_TICKS(SENSOR_BUS_TIMEOUT_MS)), exit, TAG, "bus busy");
    ESP_GOTO_ON_ERROR(bus_read(h, raw, sizeof(raw)), exit_unlock, TAG, "bus read");
    ESP_GOTO_ON_FALSE(frame_crc_ok(raw, sizeof(raw)), ESP_ERR_INVALID_CRC, exit_unlock, TAG, "bad crc");
    decode_frame(raw, out_sample);

exit_unlock:
    bus_unlock(h);
exit:
    return ret;
}
```

- Hàm có tài nguyên cần giải phóng: **1 điểm thoát** với `goto` cleanup (pattern chuẩn của ESP-IDF).
  **Vì:** tránh rò rỉ mutex/bộ nhớ khi thêm nhánh lỗi mới.
- `ESP_ERROR_CHECK()` chỉ dùng cho lỗi **không thể phục hồi lúc boot** (vd `nvs_flash_init` sau khi đã
  thử erase). Không dùng trong vòng lặp runtime. **Vì:** abort = reboot = mất dữ liệu và có thể boot-loop.
- Lỗi runtime phải có **đường phục hồi**: retry có giới hạn + backoff, về trạng thái an toàn, hoặc báo
  lên lớp trên. Không được "nuốt" lỗi im lặng; ít nhất phải `ESP_LOGW` kèm `esp_err_to_name(err)`.
- Tự định nghĩa mã lỗi riêng: dùng base `ESP_ERR_<COMPONENT>_BASE` hoặc tái dùng mã chuẩn
  (`ESP_ERR_TIMEOUT`, `ESP_ERR_INVALID_RESPONSE`, `ESP_ERR_INVALID_SIZE`...).

## 4. Bộ nhớ & buffer

- **Ưu tiên cấp phát tĩnh**; nếu cần heap thì cấp phát **1 lần lúc init**, không `malloc/free` trong
  vòng lặp chạy mãi. **Vì:** phân mảnh heap sau vài tuần làm `malloc` fail ngẫu nhiên.
- Nếu buộc phải cấp phát động: kiểm tra `NULL`, dùng `heap_caps_malloc(size, MALLOC_CAP_...)` khi cần
  DMA/internal RAM, free ở đúng 1 chỗ, gán con trỏ về `NULL` sau free.
- Cấm: `strcpy`, `strcat`, `sprintf`, `gets`, `atoi`, `strtok`. Dùng: `strlcpy`, `snprintf`
  (kiểm tra giá trị trả về ≥ size → cắt cụt), `strtol/strtoul` (kiểm tra `endptr`, `errno`, phạm vi),
  `strtok_r`.
- Luôn dùng `sizeof(buf)`, không lặp lại hằng số kích thước. Mảng tĩnh: `ARRAY_SIZE(a)` =
  `sizeof(a)/sizeof((a)[0])`.
- Kiểm tra tràn số học trước khi cộng/nhân độ dài (`if (len > sizeof(buf) - offset)`), không viết
  `offset + len > size` khi có thể tràn.
- Chuỗi nhận từ ngoài có thể **không có NUL**: dùng độ dài tường minh (`%.*s`, `memchr`).
- Buffer DMA cho SPI LCD: cấp phát `MALLOC_CAP_DMA`, căn chỉnh theo yêu cầu driver.
- Struct lưu NVS/gửi qua mạng: không dựa vào layout/padding của compiler → serialize tường minh
  (byte order, kích thước cố định) hoặc có `magic + version + crc32` như `app_config_t`.

## 5. FreeRTOS & concurrency

- Mỗi task khai báo rõ bằng macro/Kconfig: `*_TASK_STACK`, `*_TASK_PRIO`, core (C5 là single-core →
  dùng `xTaskCreate`). Ghi comment lý do chọn stack size; sau khi đo bằng
  `uxTaskGetStackHighWaterMark()` giữ margin ≥ 25%.
- Giao tiếp giữa task bằng **queue/event group/esp_event**, không bằng biến global chia sẻ. Biến dùng chung
  bắt buộc phải có mutex hoặc là `_Atomic`/kiểu nguyên tử và được comment rõ ai ghi, ai đọc.
- **Không block trong**: ISR, callback của esp-mqtt/esp_event/esp_timer, RX task của driver.
  Chỉ copy dữ liệu → `xQueueSend(..., 0)` → xử lý ở task khác. ISR chỉ dùng API `...FromISR` và
  `portYIELD_FROM_ISR`. Hàm ISR đánh dấu `IRAM_ATTR` khi cần.
- Mọi `xSemaphoreTake/xQueueReceive/xEventGroupWaitBits` có **timeout hữu hạn** và xử lý nhánh timeout.
- Thứ tự lấy lock cố định, ghi trong comment đầu file; không gọi callback của người dùng khi đang giữ lock.
  **Vì:** tránh deadlock (vd gọi `rak3172_send_cmd()` từ trong callback của rak3172).
- Tick và thời gian: dùng `esp_timer_get_time()` (int64, µs) hoặc so sánh hiệu `(now - start) >= period`
  với kiểu unsigned. Không so sánh trực tiếp `now > deadline` với `uint32_t`. **Vì:** tick counter 32-bit
  tràn sau ~49 ngày ở 1 kHz.
- Task chạy mãi phải có `vTaskDelay`/chờ có timeout trong vòng lặp và đăng ký **Task WDT**
  (`esp_task_wdt_add`) nếu là task quan trọng.

## 6. Độ bền khi chạy lâu (long-running)

- Kết nối mạng (WiFi, MQTT, LoRaWAN join): **exponential backoff có jitter**, có giới hạn trên,
  reset backoff khi thành công. Không retry dồn dập.
- Hàng đợi dữ liệu gửi có **giới hạn** và chính sách khi đầy (drop oldest + đếm số bản tin bị drop).
- Ghi NVS: chỉ ghi khi giá trị **thay đổi**, không ghi theo chu kỳ ngắn. **Vì:** flash wear.
- Log: không log trong vòng lặp nhanh ở mức INFO; rate-limit log lỗi lặp lại. Không log payload lớn.
- Theo dõi sức khỏe: heap tối thiểu (`esp_get_minimum_free_heap_size`), stack watermark, số lần reconnect,
  lý do reset (`esp_reset_reason`) → đưa vào bản tin diagnostics.
- Firmware mới sau OTA phải tự xác nhận (`esp_ota_mark_app_valid_cancel_rollback`) **chỉ sau khi**
  đã kết nối server thành công; có watchdog tự rollback nếu không xác nhận được.

## 7. Edge case bắt buộc xem xét

Với mỗi hàm/tính năng, xử lý tường minh (code hoặc comment lý do bỏ qua):

- [ ] Tham số `NULL`, độ dài 0, độ dài tối đa, độ dài vượt tối đa.
- [ ] Gọi trước `init`, gọi `init` 2 lần, gọi sau `deinit`.
- [ ] Timeout, peer không phản hồi, phản hồi thiếu/thừa/sai định dạng (AT: `AT_ERROR`, `AT_BUSY_ERROR`,
      `AT_NO_NETWORK_JOINED`, dòng bị cắt, ký tự rác sau khi module reset).
- [ ] Mất WiFi/MQTT/LoRa giữa chừng; server gửi lệnh trùng lặp hoặc lệnh cũ (replay).
- [ ] Mất điện/reset giữa lúc ghi NVS hoặc ghi OTA.
- [ ] Giá trị cảm biến ngoài dải vật lý, sensor không trả lời, NaN/Inf với `float`.
- [ ] Nút bấm: bounce, giữ lâu, bấm liên tục, bấm lúc đang boot.
- [ ] Màn hình: SPI lỗi, màn hình không gắn → hệ thống vẫn phải chạy (display là tính năng phụ).
- [ ] Tràn counter (tick, sequence number, frame counter).

## 8. Bảo mật

- MQTT chỉ dùng `mqtts://` với CA cert nhúng (`EMBED_TXTFILES`) + kiểm tra hostname. OTA dùng HTTPS
  + kiểm tra chữ ký image (Secure Boot V2 / signed app) + `secure_version` chống downgrade.
- Secret (WiFi pass, MQTT pass, AppKey) lưu **NVS encrypted**; không in ra CLI/log (hiển thị `****`).
- Lệnh downlink/OTA: kiểm tra định dạng, độ dài, phạm vi, trạng thái hệ thống trước khi thực thi;
  từ chối lệnh không hợp lệ và log cảnh báo (không log nội dung nhạy cảm).
- Không để lộ API debug/CLI nguy hiểm trong build release (`#if CONFIG_APP_DEBUG_CLI`).
- Xoá buffer chứa secret sau khi dùng (`memset` qua con trỏ `volatile` hoặc `mbedtls_platform_zeroize`).

## 9. Design pattern ưu tiên

| Pattern | Dùng khi | Ví dụ trong dự án |
|---|---|---|
| Strategy (struct con trỏ hàm) | Nhiều implementation cùng interface | `transport_if_t`, `sensor_driver_t` |
| Table-driven dispatch | Map loại → xử lý | `s_publish_routes[]`, bảng lệnh AT, bảng lệnh downlink |
| State machine tường minh (`enum` + `switch`/bảng transition) | Kết nối, join, OTA, UI | `conn_state_t`, `ota_state_t` |
| Observer (`esp_event` / callback đăng ký) | Báo sự kiện lên lớp trên | `APP_EVENT_SENSOR_READY`, button event |
| Producer–Consumer (queue) | Tách ISR/callback khỏi xử lý | RX task RAK3172 → queue → service |
| Opaque handle | Driver nhiều instance | `button_handle_t`, `lcd_handle_t` |
| Facade | Ẩn chi tiết nhiều bước | `ota_manager_start()` |

Không tạo abstraction khi chỉ có 1 implementation và không có kế hoạch thêm – ghi `TODO` thay vì
over-engineering.

## 10. Comment & tài liệu

- Comment giải thích **vì sao**, ràng buộc, context thread; không lặp lại code.
- Mỗi hàm public: mô tả, tham số (đơn vị), giá trị trả về có thể có, thread-safe hay không, có được gọi từ
  ISR/callback không.
- Giá trị chưa kiểm chứng phần cứng: `/* TODO(confirm): ... */` – reviewer sẽ tìm theo tag này.
