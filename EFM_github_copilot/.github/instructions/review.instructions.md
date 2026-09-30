---
name: 'Code review checklist'
description: 'Checklist review code firmware ESP-IDF (đúng đắn, concurrency, bộ nhớ, độ bền dài hạn, bảo mật, edge case). Dùng khi được yêu cầu review diff, PR, file hoặc đoạn code đang chọn.'
---

# Hướng dẫn review code

Bạn đóng vai **reviewer khó tính** cho firmware chạy nhiều năm ngoài hiện trường. Mục tiêu là tìm lỗi
**thật**, có thể tái hiện, không phải liệt kê ý kiến chủ quan về style (style đã có `clang-format`).

## Cách trình bày kết quả

Với mỗi vấn đề:

```
[MỨC ĐỘ] file.c:line – tóm tắt 1 dòng
Kịch bản lỗi: input/trạng thái cụ thể → hậu quả (crash, treo, rò rỉ, sai dữ liệu, lỗ hổng)
Đề xuất: diff ngắn hoặc mô tả sửa
```

Mức độ:

- **BLOCKER** – crash, deadlock, memory corruption, lỗ hổng bảo mật, brick thiết bị, mất khả năng OTA/rollback.
- **MAJOR** – rò rỉ tài nguyên, không phục hồi sau lỗi mạng, edge case chưa xử lý có thể xảy ra ngoài thực tế,
  vi phạm kiến trúc phân lớp.
- **MINOR** – khó bảo trì, thiếu log/comment quan trọng, đặt tên sai quy ước.
- **NIT** – góp ý nhỏ, không bắt buộc.

Xếp BLOCKER trước. Nếu không tìm thấy vấn đề đáng kể ở mục nào, **không bịa** – ghi "Không phát hiện vấn đề".
Kết thúc bằng: tổng số theo mức độ + **Kết luận: Approve / Request changes**.

## Checklist

### Đúng đắn
- [ ] Mọi `esp_err_t` trả về đều được kiểm tra; nhánh lỗi giải phóng đúng tài nguyên (mutex, heap, handle).
- [ ] Không dùng biến chưa khởi tạo; switch trên enum có đủ case hoặc `default` hợp lý.
- [ ] Đơn vị đúng (ms ↔ tick dùng `pdMS_TO_TICKS`, byte ↔ phần tử).
- [ ] So sánh thời gian an toàn khi tràn counter.
- [ ] `float`: xử lý NaN/Inf, không so sánh bằng `==`.

### Bộ nhớ
- [ ] Không có hàm chuỗi không giới hạn (`strcpy`, `sprintf`, `strcat`); `snprintf` kiểm tra cắt cụt.
- [ ] Kiểm tra độ dài trước `memcpy`; không tràn số học khi tính offset.
- [ ] Không `malloc` trong vòng lặp runtime; mọi `malloc` có kiểm tra NULL và có đúng 1 `free`.
- [ ] Không trả về/lưu con trỏ tới buffer tạm (stack, buffer của callback).
- [ ] Stack size task hợp lý so với buffer local lớn (buffer > 256 byte trên stack cần xem lại).

### Concurrency / FreeRTOS
- [ ] Dữ liệu dùng chung có bảo vệ (mutex/atomic/critical section) và comment ai ghi/ai đọc.
- [ ] Không block trong ISR, callback esp-mqtt/esp_event/esp_timer, RX task của driver.
- [ ] Mọi thao tác chờ có timeout hữu hạn; nhánh timeout được xử lý.
- [ ] Không gọi API có thể lấy lại cùng lock từ trong callback (deadlock).
- [ ] ISR chỉ dùng API `FromISR`, có `IRAM_ATTR` nếu cần chạy khi cache flash tắt.

### Độ bền dài hạn
- [ ] Reconnect có backoff + jitter + giới hạn; không retry vô hạn dồn dập.
- [ ] Queue/buffer có giới hạn và chính sách khi đầy.
- [ ] Không ghi NVS/flash theo chu kỳ ngắn.
- [ ] Task quan trọng đăng ký Task WDT; không tắt WDT.
- [ ] Log không flood; không log trong ISR.
- [ ] OTA: không phá luồng mark-valid/rollback; không cho OTA khi đang bận việc quan trọng hoặc pin yếu.

### Bảo mật
- [ ] Không hard-code/log secret; CLI che secret.
- [ ] TLS kiểm tra cert và hostname; không có `skip_cert_common_name_check` hay `INSECURE`.
- [ ] Input từ ngoài (downlink MQTT/LoRa, AT response, CLI, NVS) được validate độ dài/phạm vi/định dạng.
- [ ] Lệnh từ server được kiểm tra hợp lệ và chống replay/trùng nếu có tác động vật lý hoặc thay đổi cấu hình.
- [ ] Không bật debug/JTAG/CLI nguy hiểm trong cấu hình release.

### Kiến trúc & tái sử dụng
- [ ] Không vi phạm phân lớp (driver không include service/app).
- [ ] Thêm loại bản tin/lệnh bằng bảng định tuyến, không thêm hàm public mới cho từng loại.
- [ ] Không trùng lặp logic đã có ở component khác (tìm trước khi viết mới).
- [ ] API public có comment đủ: tham số, đơn vị, lỗi trả về, thread-safety.

### Build & tài liệu
- [ ] Thay đổi Kconfig/partition/NVS schema/topic/payload có cập nhật `docs/` và (nếu phá tương thích) ADR.
- [ ] Giá trị phần cứng chưa kiểm chứng được đánh dấu `TODO(confirm):`.
- [ ] Commit message đúng `.github/instructions/commit.instructions.md`.
