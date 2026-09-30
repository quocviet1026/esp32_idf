---
name: add-message-type
description: 'Thêm 1 loại bản tin uplink (thiết bị → server) hoặc lệnh downlink (server → thiết bị) theo kiến trúc table-driven của transport, cho cả WiFi/MQTT và LoRaWAN. Dùng khi cần gửi dữ liệu mới hoặc hỗ trợ lệnh mới từ server.'
argument-hint: '<uplink|downlink> <TEN_LOAI> <mô tả dữ liệu> [mqtt|lorawan|both]'
---

# Thêm loại bản tin / lệnh

Đọc `components/transport/include/transport.h`, `transport_wifi_mqtt.c`, `transport_lorawan.c`,
`main/main.c` và [docs/dataflow.md](../../../docs/dataflow.md) trước khi sửa.

## Uplink (APP_MSG_*)

1. `transport.h`: thêm `APP_MSG_<X>` vào `app_msg_type_t`; nếu có dữ liệu kèm theo → thêm nhánh vào union
   `app_message_t.data` (kiểu kích thước cố định, có đơn vị trong tên).
2. Mỗi transport được hỗ trợ: viết `encode_<x>_payload()` riêng và thêm 1 dòng vào `s_publish_routes[]`.
   - MQTT: JSON gọn, có `"v"` (schema version), `"ts"`, `"seq"`; kiểm tra `snprintf` cắt cụt.
   - LoRaWAN: nhị phân fixed-point, big-endian, chọn fPort riêng, **kích thước ≤ max payload của DR
     thấp nhất đang dùng** – tra bảng *LoRaWAN Regional Parameters* cho AS923 (lưu ý dwell time 400 ms
     làm giảm max payload), không tự nhớ con số. Nếu vượt, báo người dùng.
   - Transport không hỗ trợ: trả `ESP_ERR_NOT_SUPPORTED` (không crash).
3. `main.c`: thêm collector + dòng vào bảng lịch của transport tương ứng (interval LoRaWAN phải tôn trọng
   duty-cycle – ghi `TODO(confirm)` nếu chưa tính airtime).

## Downlink (APP_CMD_*)

1. `transport.h`: thêm `APP_CMD_<X>` + dữ liệu đã decode vào union `app_command_t`.
2. Decoder trong từng transport: **validate** độ dài, phạm vi, version; lệnh sai → `APP_CMD_UNKNOWN` + log WARN.
3. `main.c::on_downlink_command()`: thêm case; không block trong callback – đẩy sang task xử lý nếu lâu.
4. Lệnh thay đổi cấu hình/tác động vật lý: kiểm tra trạng thái hệ thống, chống lặp (seq/id), phản hồi ACK.

## Hoàn tất

- Thêm host test cho encode/decode (biên, sai định dạng, cắt cụt).
- Cập nhật mục 9 "Định dạng bản tin" trong `docs/dataflow.md`. Đổi định dạng bản tin đã có → `BREAKING CHANGE`.
