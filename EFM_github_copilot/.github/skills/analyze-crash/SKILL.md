---
name: analyze-crash
description: 'Phân tích log crash/panic/watchdog/reset của ESP32-C5 (Guru Meditation, backtrace RISC-V, stack overflow, TWDT, brownout) và đề xuất nguyên nhân gốc + cách sửa. Dùng khi người dùng dán log lỗi hoặc thiết bị reset bất thường.'
argument-hint: '<dán log monitor hoặc đường dẫn file log>'
---

# Phân tích crash

1. Xác định loại sự cố từ log: `Guru Meditation Error` (loại exception, `MEPC`, `MTVAL`),
   `Stack protection fault`/`stack overflow in task`, `Task watchdog got triggered`, `Brownout detector`,
   `abort() was called` (tìm assert/`ESP_ERROR_CHECK` phía trên), `rst:` reason trong boot log.
2. Giải mã địa chỉ: nếu log chưa được `idf.py monitor` decode, dùng
   `riscv32-esp-elf-addr2line -pfiaC -e build/<app>.elf <addr...>` với **đúng file ELF của bản đang chạy**
   (kiểm tra `App version`/`ELF file SHA256` trong boot log khớp với build).
3. Đọc code tại các frame, truy ngược dữ liệu gây lỗi. Giả thuyết thường gặp theo loại:
   - Load/Store access fault, `MTVAL` gần 0 → con trỏ NULL / struct chưa init.
   - Stack overflow → buffer lớn trên stack, đệ quy, `printf` float trong task stack nhỏ.
   - TWDT → vòng lặp không yield, chờ không timeout, lock bị giữ lâu, flash erase lớn.
   - Crash ngẫu nhiên sau nhiều giờ → heap corruption, use-after-free, race condition, tràn buffer.
4. Trả lời: nguyên nhân gốc (mức độ chắc chắn), bằng chứng từ log, đề xuất sửa dạng diff, và cách
   xác minh (bật `CONFIG_HEAP_POISONING_COMPREHENSIVE`, `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK`,
   core dump vào flash). Nếu log không đủ, nói rõ cần thêm thông tin gì – không đoán.
