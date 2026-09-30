---
name: 'Firmware testing rules'
description: 'Quy tắc viết unit test (Unity), host test (linux target) và kịch bản test phần cứng cho firmware ESP-IDF. Dùng khi tạo hoặc sửa test.'
applyTo: '**/test/**,**/test_apps/**,**/host_test/**'
---

# Quy tắc test

## Chiến lược 3 tầng

| Tầng | Công cụ | Nội dung | Chạy ở |
|---|---|---|---|
| Host test | ESP-IDF `linux` target + Unity, hoặc CMock | Logic thuần: parser AT, encode/decode payload, state machine, ring buffer, validate config | CI, mỗi PR |
| Target unit test | `test_apps/` + `pytest-embedded` | Driver với phần cứng thật (UART loopback, SPI, NVS) | Bench, trước release |
| System/HIL test | Kịch bản thủ công hoặc script | Join LoRaWAN, MQTT reconnect, OTA + rollback, soak test 72h | Trước release |

**Vì:** phần lớn lỗi nằm ở logic thuần (parser, encode) – tách chúng khỏi phụ thuộc phần cứng để test
nhanh trên host. Muốn test được, code nghiệp vụ phải nhận dependency qua interface (Strategy/con trỏ hàm),
không gọi thẳng driver.

## Quy tắc viết test

- Tên test: `TEST_CASE("<unit>: <hành vi> when <điều kiện>", "[<component>]")`.
- Mỗi test kiểm tra **1 hành vi**, theo cấu trúc Arrange – Act – Assert.
- Bắt buộc có test cho: input hợp lệ, **biên** (0, max, max+1), input sai định dạng, NULL, timeout,
  gọi trước init.
- Parser AT: test với dòng bị cắt, ký tự rác, `\r` không có `\n`, dòng dài hơn buffer, nhiều `+EVT` xen
  giữa phản hồi đồng bộ.
- Không phụ thuộc thứ tự chạy test; dọn dẹp tài nguyên (`tearDown`), kiểm tra rò rỉ heap
  (`unity_utils_check_leak` / so sánh `heap_caps_get_free_size` trước và sau).
- Không dùng `vTaskDelay` dài để "chờ cho chắc" – dùng đồng bộ tường minh (semaphore) có timeout.

## Kịch bản test phần cứng tối thiểu trước release

1. Boot lần đầu với NVS trống → vào cấu hình mặc định, CLI hoạt động.
2. Mất WiFi 10 phút → tự kết nối lại, không mất dữ liệu trong giới hạn queue.
3. Server MQTT tắt/bật → reconnect có backoff.
4. RAK3172 bị reset giữa lúc gửi → driver resync, rejoin.
5. OTA thành công; OTA với image hỏng/sai chữ ký → từ chối; image boot fail → rollback.
6. Mất điện giữa lúc OTA và giữa lúc ghi NVS → boot lại bình thường.
7. Soak test ≥ 72 giờ: heap tối thiểu ổn định, không reset ngoài dự kiến (`esp_reset_reason`).
8. Màn hình tháo ra → firmware vẫn chạy, log cảnh báo 1 lần.
