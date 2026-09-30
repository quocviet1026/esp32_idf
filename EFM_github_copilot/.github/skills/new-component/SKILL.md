---
name: new-component
description: 'Tạo khung 1 ESP-IDF component mới đúng cấu trúc của dự án (include/, internal header, Kconfig, CMakeLists, init/deinit idempotent, test host). Dùng khi cần thêm driver hoặc service mới.'
argument-hint: '<ten_component> <driver|service> <mô tả trách nhiệm>'
---

# Tạo component mới

Trước khi sinh file, **xác nhận với người dùng** (nếu argument chưa đủ):
tên component, lớp (driver/service), trách nhiệm 1 câu, có nhiều instance không (→ opaque handle),
có task riêng không (stack/priority), phát sự kiện gì, phụ thuộc component nào.
Tìm trong `components/` xem đã có component làm việc tương tự chưa – nếu có, đề xuất mở rộng thay vì tạo mới.

## Cấu trúc sinh ra

```
components/<name>/
├── CMakeLists.txt          # REQUIRES tối thiểu, -Wall -Wextra -Werror
├── Kconfig                 # mọi hằng số phần cứng/tuning, có range + help
├── include/<name>.h        # API public, comment đầy đủ
├── <name>_internal.h       # kiểu/hàm nội bộ (nếu cần)
├── <name>.c
└── test/host/test_<name>.c # Unity test cho logic thuần
```

## Khung API bắt buộc

```c
/* Single instance */
esp_err_t <name>_init(const <name>_config_t *cfg);   /* ESP_ERR_INVALID_STATE neu da init */
esp_err_t <name>_deinit(void);                       /* an toan khi chua init */

/* Multi instance */
typedef struct <name>_obj *<name>_handle_t;
esp_err_t <name>_create(const <name>_config_t *cfg, <name>_handle_t *out_handle);
esp_err_t <name>_delete(<name>_handle_t handle);
```

- Config struct có macro `<NAME>_CONFIG_DEFAULT()` lấy giá trị từ Kconfig.
- Callback đăng ký kèm `void *user_ctx`.
- Nếu có task: tạo trong init, dừng sạch trong deinit (event bit + chờ task tự xóa, có timeout).

Tuân thủ [quy tắc C/ESP-IDF](../../instructions/c-esp-idf.instructions.md) và
[quy tắc build](../../instructions/cmake-kconfig.instructions.md). Sau khi tạo: thêm component vào
mục 3 "Danh sách component" trong `docs/architecture.md`, chạy `idf.py build` và báo kết quả.
