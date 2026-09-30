# ESP32 — OTA (Over The Air Updates): Tổng hợp chi tiết cho Kỹ sư

> Tổng hợp dựa trên: [ESP-IDF OTA Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html)

---

## 1. Tổng quan cơ chế — 2 chế độ Update

### 1.1. Safe Update Mode (an toàn khi mất điện)

```
Yêu cầu tối thiểu: 2 OTA app slot (ota_0, ota_1) + 1 OTA Data Partition
```

Cơ chế: OTA luôn ghi vào slot **đang không chạy**. Chỉ sau khi image mới được verify thành công, `otadata` mới được cập nhật để trỏ sang slot mới.

→ **Mất điện giữa chừng** lúc ghi → slot đang chạy không bị đụng tới → thiết bị vẫn boot bình thường từ firmware cũ.

Áp dụng cho: **Application** (kiến trúc A/B partition kinh điển).

### 1.2. Unsafe Update Mode (không an toàn khi mất điện)

Cơ chế: ghi vào 1 partition tạm, sau khi tải xong mới copy sang partition đích cuối cùng. Nếu mất điện đúng lúc copy → partition đích có thể bị hỏng dở dang, **không có fallback**.

Áp dụng cho: **Bootloader, Partition Table, các data partition khác (NVS, FAT...)**.

⚠️ **Lưu ý kỹ sư**: ESP-IDF OTA **không hỗ trợ cập nhật an toàn** cho bootloader/partition table — cần kế hoạch dự phòng riêng nếu buộc phải đổi 2 thứ này ngoài field.

---

## 2. OTA Data Partition

### Cấu trúc bắt buộc

```
Type = data, SubType = ota
```

### Hành vi factory default (chưa từng OTA)

```
otadata trống (all 0xFF)
     │
Có partition "factory"? → Boot vào factory
     │
Không có → Boot vào OTA slot đầu tiên (thường ota_0)
```

### Cơ chế chống hỏng dữ liệu — thiết kế 2 sector

```
Kích thước: 2 sector × flash sector size = 0x2000 bytes (8KB)
```

- 2 sector ghi **cùng nội dung** (redundant), erase độc lập.
- Nếu 2 sector không khớp (mất điện giữa chừng) → dùng **counter field** để xác định sector nào mới hơn.

---

## 3. App Rollback — cơ chế tự phục hồi khi firmware mới lỗi

### 3.1. Máy trạng thái (6 states)

| State | Bootloader có chọn boot không? |
|---|---|
| `ESP_OTA_IMG_VALID` | ✅ Không giới hạn |
| `ESP_OTA_IMG_UNDEFINED` | ✅ Không giới hạn |
| `ESP_OTA_IMG_NEW` | ✅ Chỉ 1 lần — sau đó tự chuyển `PENDING_VERIFY` |
| `ESP_OTA_IMG_PENDING_VERIFY` | ⚠️ Nếu boot lại vẫn ở state này → tự chuyển `ABORTED` |
| `ESP_OTA_IMG_INVALID` | ❌ Không được chọn |
| `ESP_OTA_IMG_ABORTED` | ❌ Không được chọn |

**Lưu ý quan trọng**: state **không nằm trong file `.bin`** của app, mà nằm trong chính partition `otadata` (cùng chỗ với `ota_seq` counter).

### 3.2. Luồng Rollback đầy đủ

```
1. OTA thành công → esp_ota_set_boot_partition() → state = NEW
        │
2. esp_restart()
        │
3. [Bootloader] App cũ có state PENDING_VERIFY? → đổi thành ABORTED
        │
4. [Bootloader] Chọn app boot, bỏ qua app INVALID/ABORTED
        │
5. [Bootloader] App được chọn có state NEW → đổi thành PENDING_VERIFY
        │
6. App mới khởi động, state = PENDING_VERIFY
        │
7. App tự chạy self-test/diagnostic
        │
   Pass → esp_ota_mark_app_valid_cancel_rollback() → state = VALID
   Fail → esp_ota_mark_app_invalid_rollback_and_reboot() → rollback về app cũ
```

### 3.3. Code mẫu chính thức

```c
const esp_partition_t *running = esp_ota_get_running_partition();
esp_ota_img_states_t ota_state;
if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        bool diagnostic_is_ok = diagnostic();  // tự viết self-test
        if (diagnostic_is_ok) {
            esp_ota_mark_app_valid_cancel_rollback();
        } else {
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }
}
```

**Khuyến nghị**: hàm `diagnostic()` nên chạy càng sớm càng tốt trong `app_main()` — nếu app crash/mất điện **trước khi** kịp gọi 1 trong 2 hàm confirm, bootloader tự động rollback ở lần boot tiếp theo (bảo vệ ngầm định, không cần code thêm).

### 3.4. Giới hạn — Factory không rollback được

> Only `OTA` partitions can be rolled back. Factory partition is not rolled back.

Nếu không có `factory`, khi tất cả OTA slot đều invalid → không còn nơi rollback về.

### 3.5. Boot lại app đã Invalid (dùng để debug)

```c
const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();
esp_ota_set_boot_partition(last_invalid);
esp_restart();
```

### 3.6. Nơi các state được set

| State | Set bởi |
|---|---|
| `VALID` | `esp_ota_mark_app_valid_cancel_rollback()` |
| `UNDEFINED` | `esp_ota_set_boot_partition()` nếu rollback **không** bật |
| `NEW` | `esp_ota_set_boot_partition()` nếu rollback **có** bật |
| `INVALID` | `esp_ota_mark_app_invalid_rollback()` / `..._and_reboot()` |
| `ABORTED` | Tự động nếu không confirm mà reboot |
| `PENDING_VERIFY` | Bootloader tự set khi thấy state `NEW` |

---

## 4. Anti-Rollback — chống hạ cấp firmware

### Khác biệt với App Rollback

| | App Rollback | Anti-Rollback |
|---|---|---|
| Mục đích | Tự quay lại bản cũ khi bản mới lỗi | Ngăn cài bản **cũ hơn** version bảo mật đã biết |
| Hướng | Về quá khứ | Chặn đi lùi |

### Cơ chế

```
Kconfig: CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
```

- Mỗi app có `secure_version` (lưu trong `esp_app_desc_t`, cấu hình qua `CONFIG_BOOTLOADER_APP_SECURE_VERSION`).
- Chip lưu `secure_version` hiện tại trong **eFuse** (`EFUSE_BLK3_RDATA4_REG` trên ESP32) — dùng cơ chế **đếm số bit đã set** (eFuse chỉ set 0→1, không revert được).
- App muốn boot phải có `secure_version` **≥** giá trị trong eFuse.

### Kết hợp với App Rollback

Khi cả 2 bật: rollback về app cũ chỉ khả thi nếu `secure_version` của app đó vẫn ≥ eFuse hiện tại. **Sau khi tăng `secure_version`, các app cũ hơn vĩnh viễn không rollback về được nữa.**

### Giới hạn kỹ thuật cứng — QUAN TRỌNG

> Số bit trong trường `secure_version` giới hạn **32 bit** → chỉ tối đa **32 lần** tăng version trong suốt vòng đời sản phẩm (có thể giảm qua `CONFIG_BOOTLOADER_APP_SEC_VER_SIZE_EFUSE_FIELD`, đồng nghĩa giảm số lần dùng được).

**Khuyến nghị**: chỉ tăng `secure_version` khi có bản vá bảo mật nghiêm trọng thật sự, không tăng cho release tính năng thông thường.

### Ràng buộc khác

- Chỉ hoạt động khi encoding scheme eFuse = `NONE`.
- **Không hỗ trợ** partition `factory` và `test` khi bật Anti-Rollback.

### Tối ưu — kiểm tra sớm trước khi tải hết image

```c
bool image_header_was_checked = false;
while (1) {
    int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
    if (data_read > 0) {
        if (!image_header_was_checked) {
            esp_app_desc_t new_app_info;
            if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                if (!esp_efuse_check_secure_version(new_app_info.secure_version)) {
                    ESP_LOGE(TAG, "Secure version thấp hơn eFuse, hủy tải.");
                    http_cleanup(client);
                    task_fatal_error();
                }
                image_header_was_checked = true;
                esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
            }
        }
        esp_ota_write(update_handle, ota_write_data, data_read);
    }
}
```

---

## 5. Secure OTA không cần Secure Boot phần cứng

```
Kconfig: CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT
        + CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT
```

Cho phép verify chữ ký số image OTA **mà không cần** bật Secure Boot phần cứng (yêu cầu burn eFuse, không đảo ngược). Phù hợp giai đoạn phát triển/testing.

---

## 6. Signed Data Partition Updates

Mở rộng verify chữ ký (Secure Boot v2) sang cả **data partition thường** (subtype `ESP_PARTITION_SUBTYPE_DATA_UNDEFINED`), không chỉ app.

```bash
idf.py secure-sign-data --keyfile PRIVATE_SIGNING_KEY --output signed_data.bin data.bin
```

- Khóa ký phải trùng khóa ký app (public key digest đã burn eFuse).
- Format: data (pad tới 4KB) + 4KB signature block.
- Ứng dụng: OTA file config/model AI/license cần chống giả mạo.

---

## 7. Tuning OTA Performance

| Kỹ thuật | Cách làm | Lưu ý |
|---|---|---|
| Bulk erase thay vì tuần tự | `esp_https_ota_config_t::bulk_flash_erase = true` | Có thể trigger Task Watchdog nếu partition lớn → tăng watchdog timeout |
| Tăng buffer size HTTP | `esp_https_ota_config_t::http_config::buffer_size` | Giảm round-trip, tăng throughput |
| Chọn loại bộ nhớ cho buffer | `buffer_caps = MALLOC_CAP_INTERNAL` | Nếu có SPIRAM, ép buffer nằm internal RAM (nhanh hơn PSRAM) |
| Tối ưu mạng | Xem "Improving Network Speed" trong Speed Optimization guide | Ngoài phạm vi OTA riêng |

---

## 8. Công cụ `otatool.py`

Component `app_update`, dùng test/debug OTA logic **qua serial**, không cần OTA qua mạng.

### Python API

```python
import sys, os
idf_path = os.environ["IDF_PATH"]
sys.path.append(os.path.join(idf_path, "components", "app_update"))
from otatool import *

target = OtatoolTarget("/dev/ttyUSB1")
target.erase_otadata()                          # reset về factory app
target.erase_ota_partition(0)
target.switch_ota_partition(1)
target.read_ota_partition("ota_3", "ota_3.bin")
```

### Command-line

```bash
otatool.py --port "/dev/ttyUSB1" erase_otadata
otatool.py --port "/dev/ttyUSB1" erase_ota_partition --slot 0
otatool.py --port "/dev/ttyUSB1" switch_ota_partition --slot 1
otatool.py --port "/dev/ttyUSB1" read_ota_partition --name=ota_3 --output=ota_3.bin
```

**Ứng dụng**: giả lập kịch bản lỗi (partition hỏng, otadata sai...) mà không cần dựng cả pipeline OTA qua mạng thật.

---

## 9. API Reference — Tóm tắt các hàm cốt lõi

### Nhóm ghi image mới

| Hàm | Vai trò |
|---|---|
| `esp_ota_begin()` | Bắt đầu OTA — erase partition đích, trả handle |
| `esp_ota_resume()` | Tiếp tục OTA bị gián đoạn — không erase lại |
| `esp_ota_write()` | Ghi tuần tự dữ liệu |
| `esp_ota_write_with_offset()` | Ghi không tuần tự (gói đến sai thứ tự, VD qua BLE) |
| `esp_ota_set_final_partition()` | Chỉ định partition đích khác staging partition |
| `esp_ota_end()` | Kết thúc, verify image, copy sang final partition |
| `esp_ota_abort()` | Hủy giữa chừng |

### Nhóm chọn boot partition

| Hàm | Vai trò |
|---|---|
| `esp_ota_set_boot_partition()` | Verify + chọn partition boot tiếp theo |
| `esp_ota_set_boot_partition_skip_validate()` | Chọn boot không verify (cẩn thận) |
| `esp_ota_get_boot_partition()` | Partition đã cấu hình để boot lần tới |
| `esp_ota_get_running_partition()` | Partition đang thực sự chạy |
| `esp_ota_get_next_update_partition()` | Tự tìm slot tiếp theo (round-robin) |

### Nhóm Rollback

| Hàm | Vai trò |
|---|---|
| `esp_ota_mark_app_valid_cancel_rollback()` | Xác nhận app hoạt động tốt |
| `esp_ota_mark_app_invalid_rollback()` | Rollback không reboot |
| `esp_ota_mark_app_invalid_rollback_and_reboot()` | Rollback có reboot |
| `esp_ota_get_last_invalid_partition()` | Partition invalid gần nhất |
| `esp_ota_get_state_partition()` | Đọc state hiện tại |
| `esp_ota_check_rollback_is_possible()` | Kiểm tra trước khả thi rollback |
| `esp_ota_erase_last_boot_app_partition()` | Xóa app cũ sau khi confirm app mới ổn |

### Mã lỗi quan trọng

| Mã lỗi | Ý nghĩa | Xử lý gợi ý |
|---|---|---|
| `ESP_ERR_OTA_ROLLBACK_INVALID_STATE` | App hiện tại chưa confirm (`PENDING_VERIFY`) mà cố OTA tiếp | Confirm app trước bằng `esp_ota_mark_app_valid_cancel_rollback()` |
| `ESP_ERR_OTA_PARTITION_CONFLICT` | Cố ghi đè partition đang chạy | Dùng `esp_ota_get_next_update_partition()` |
| `ESP_ERR_OTA_VALIDATE_FAILED` | Image không hợp lệ / chữ ký sai | Giữ nguyên app cũ |
| `ESP_ERR_OTA_SMALL_SEC_VER` | `secure_version` thấp hơn eFuse | Từ chối cập nhật |
| `ESP_ERR_OTA_SELECT_INFO_INVALID` | `otadata` hỏng | Cần `erase_otadata` để reset |

---

## 10. Application Examples chính thức

### 10.1. `native_ota_example`

Minh họa **native API** của `app_update` (không qua lớp bọc `esp_https_ota`) — tự quản lý HTTP client, tự gọi `esp_ota_write()`, tự verify, tự confirm rollback. Phù hợp khi cần hiểu rõ từng bước thô hoặc build client OTA tùy biến sâu.

Dùng partition table `CONFIG_PARTITION_TABLE_TWO_OTA` (3 app partition: `factory`, `ota_0`, `ota_1`).

Chứa sẵn code mẫu `diagnostic()` — chính là code App Rollback đã mô tả ở mục 3.

### 10.2. `otatool` example

Minh họa cách dùng `otatool.py` để thao tác trực tiếp OTA partition **qua serial**, không cần OTA qua mạng. App phía firmware cực đơn giản (chỉ in ra partition đang chạy) — logic thật nằm ở script Python trên máy tính.

**Khi nào dùng cái nào:**

```
Giai đoạn PHÁT TRIỂN LOGIC (rollback, anti-rollback...)
        │
   otatool → test nhanh, lặp lại nhiều kịch bản, không cần mạng
        │
        ▼
Giai đoạn TRIỂN KHAI THẬT (client field)
        │
   native_ota_example → nền tảng viết code thật, tải qua HTTPS
```

### 10.3. Các example liên quan khác (dùng `esp_https_ota` — lớp trừu tượng)

- `simple_ota_example` — dùng API đơn giản hóa của `esp_https_ota`.
- `advanced_https_ota` — tương tự, có thêm tùy chọn nâng cao.
- `partitions_ota` — minh họa Signed Data Partition Updates (mục 6).

---

## 11. Hướng dẫn — Dựng HTTPS Server local để test OTA

### Vì sao khả thi

ESP32 chỉ cần kết nối TCP/IP tới 1 IP:port chạy HTTPS — không quan tâm server là cloud hay laptop, miễn là **cùng LAN** và không bị firewall chặn.

### Bước 1 — Tạo self-signed certificate

```bash
openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365
```

⚠️ **Common Name (CN)** phải nhập đúng **IP local** của máy tính (VD: `192.168.1.100`).

### Bước 2 — Nhúng chứng chỉ vào firmware

`main/CMakeLists.txt`:
```cmake
idf_component_register(...
    EMBED_TXTFILES server_certs/ca_cert.pem
)
```

Trong code:
```c
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[]   asm("_binary_ca_cert_pem_end");
```

### Bước 3 — Chạy HTTPS server bằng Python

```python
import http.server, ssl

server_address = ('0.0.0.0', 8070)
httpd = http.server.HTTPServer(server_address, http.server.SimpleHTTPRequestHandler)
httpd.socket = ssl.wrap_socket(httpd.socket,
                                keyfile="ca_key.pem",
                                certfile="ca_cert.pem",
                                server_side=True)
httpd.serve_forever()
```

```bash
python3 ota_server.py
# → Starting HTTPS server at https://192.168.1.100:8070
```

### Bước 4 — Cấu hình ESP32 trỏ tới server local

```
idf.py menuconfig
→ Example Configuration → Firmware Upgrade URL
→ https://192.168.1.100:8070/app.bin
```

### Vấn đề hay gặp — CN/SAN mismatch

Nếu TLS handshake fail, bật (chỉ khi test):
```
idf.py menuconfig
→ Component config → ESP-TLS → Skip server certificate CN fieldcheck
```
⚠️ **Không dùng option này cho production** — vô hiệu hóa 1 lớp bảo mật quan trọng.

### Cách đơn giản hơn — HTTP thường (chỉ test logic, không test TLS)

```bash
python3 -m http.server 8070
```
Trỏ URL `http://192.168.1.100:8070/app.bin`, dùng `esp_http_client` thay vì `esp_https_ota`.

### Bảng chọn cách test theo mục đích

| Mục đích | Nên dùng |
|---|---|
| Test full flow thật (có TLS) | HTTPS + self-signed cert + `esp_https_ota` |
| Test nhanh logic Rollback/Anti-Rollback/chọn slot | HTTP thường |
| Test không cần tải qua mạng | `otatool.py` |
| Muốn công cụ dựng sẵn | [`tomekceszke/ota-server`](https://github.com/tomekceszke/ota-server) |

---

## 12. Checklist thiết kế OTA cho Production

| Hạng mục | Khuyến nghị |
|---|---|
| Partition table | Tối thiểu 2 OTA slot + `otadata`; cân nhắc giữ `factory` làm phao cứu sinh |
| App Rollback | Luôn bật `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, confirm/rollback sớm nhất trong `app_main()` |
| Anti-Rollback | Chỉ tăng `secure_version` cho bản vá bảo mật nghiêm trọng (giới hạn cứng 32 lần) |
| Bảo mật khi chưa Secure Boot | Cân nhắc `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` |
| Data quan trọng ngoài app | Dùng Signed Data Partition nếu cần chống giả mạo |
| Hiệu năng | Bulk erase + buffer size + `MALLOC_CAP_INTERNAL` nếu có PSRAM |
| Testing | Dùng `otatool.py` trước khi test qua mạng thật; dùng HTTPS local server để test full flow |
| Xử lý lỗi | Bắt đầy đủ mã lỗi `ESP_ERR_OTA_*`, đặc biệt `ROLLBACK_INVALID_STATE` |
| Bảo mật production | Không để "Skip CN fieldcheck" bật khi lên production |

---

## 13. Nguồn tham khảo

- [Over The Air Updates (OTA) — ESP-IDF Official Docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html)
- [ESP HTTPS OTA — API Reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_https_ota.html)
- [Partition Tables](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html)
- [`system/ota/native_ota_example`](https://github.com/espressif/esp-idf/tree/v6.1/examples/system/ota/native_ota_example)
- [`system/ota/otatool`](https://github.com/espressif/esp-idf/tree/v6.1/examples/system/ota/otatool)
- [`system/ota/partitions_ota`](https://github.com/espressif/esp-idf/tree/v6.1/examples/system/ota/partitions_ota)
- [README chung cho toàn bộ OTA examples](https://github.com/espressif/esp-idf/blob/master/examples/system/ota/README.md)
- [esp32.com forum — hướng dẫn tạo cert cho OTA example](https://esp32.com/viewtopic.php?p=31546)
- [`tomekceszke/ota-server`](https://github.com/tomekceszke/ota-server) — server test dựng sẵn
