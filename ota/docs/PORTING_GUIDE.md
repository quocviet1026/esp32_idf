# Hướng dẫn Porting `ota_manager` sang project mới đã có sẵn WiFi + MQTT

> Tài liệu này hướng dẫn từng bước **cụ thể** (gọi hàm nào, ở đâu, tham số gì) để đưa tính năng OTA từ project `esp/ota` sang 1 project khác (vd project ESP32-C5 đang chạy, đã có WiFi + MQTT). Xem đánh giá thiết kế tổng quan + các điều kiện cần verify tại phần trao đổi trước, và kế hoạch retrofit đầy đủ (partition table, Secure Boot...) tại `RETROFIT_OTA_ESP32C5.md`. File này chỉ tập trung vào **thao tác code cụ thể**.

---

## Tổng quan luồng chạy (Runtime Flow) của tính năng OTA hiện tại

Trước khi porting, cần hiểu rõ **thứ tự thực thi thật sự** của toàn bộ các hàm trong tính năng OTA — vì khi port sang project khác, đúng những điểm nối trong các luồng này (in đậm) là chỗ cần chèn lời gọi hàm.

### A. Luồng khởi động (boot) — tới lúc sẵn sàng nhận lệnh OTA

```
app_main()                                                    [main.c]
  │
  ├─ log_partition_table()                                    [main.c]
  ├─ nvs_flash_init()
  ├─ ► ota_rollback_start_confirm_watchdog(2*60*1000)          [ota_rollback.c] ← DIEM NOI #4
  │       └─ neu dang PENDING_VERIFY: dat hen gio, qua han ma chua confirm
  │          se CHU DONG esp_ota_mark_app_invalid_rollback_and_reboot()
  │
  ├─ wifi_manager_start()                                     [wifi_manager component]
  │     └─ esp_wifi_connect() (async, tu retry khi mat song)
  │
  └─ esp_event_handler_register(IP_EVENT_STA_GOT_IP, ip_event_handler)   [main.c]

... (bat dong bo, cho WiFi co IP) ...

ip_event_handler(IP_EVENT_STA_GOT_IP)                          [main.c]
  └─ mqtt_start()                                              [main.c]
        └─ esp_mqtt_client_init() + esp_mqtt_client_start()

... (bat dong bo, cho MQTT connect) ...

mqtt_event_handler(MQTT_EVENT_CONNECTED)                       [main.c]
  ├─ publish version len VERSION_TOPIC
  ├─ subscribe cac topic "test"
  ├─ ► ota_manager_start(client)                                [ota_manager.c]  ← DIEM NOI #1
  │       └─ esp_mqtt_client_subscribe(client, OTA_COMMAND_TOPIC)
  │
  └─ ► ota_rollback_confirm_if_pending()                        [ota_rollback.c] ← DIEM NOI #3
          └─ neu state == PENDING_VERIFY → esp_ota_mark_app_valid_cancel_rollback()
```
→ Sau bước này, thiết bị đã subscribe xong topic lệnh OTA, sẵn sàng nhận trigger, và đã tự confirm rollback nếu vừa mới OTA xong ở lần chạy trước.

### B. Luồng nhận lệnh trigger OTA qua MQTT

```
Server publish {"version":"x.y.z","url":"https://..."} len OTA_COMMAND_TOPIC
        │
mqtt_event_handler(MQTT_EVENT_DATA)                            [main.c]
  └─ ► ota_manager_handle_mqtt_data(event)                      [ota_manager.c] ← DIEM NOI #2
        ├─ parse JSON (cJSON)
        ├─ is_version_newer(version)?  → so voi esp_ota_get_running_partition()
        ├─ dang co s_ota_in_progress?  → neu co, bo qua
        └─ neu hop le → s_ota_in_progress = true
              └─ ota_task_start(client, url)                    [ota_task.c]
                    └─ xTaskCreate(&ota_task, ...)  (task rieng, stack 8192)
```

### C. Luồng thực thi task OTA (chạy trong task riêng do `ota_task_start` tạo ra)

```
ota_task(pvParameter)                                          [ota_task.c]
  │
  ├─ esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ota_event_handler)
  │
  ├─ esp_https_ota_begin(&ota_config, &handle)
  │     ├─ THANH CONG → dispatch ESP_HTTPS_OTA_CONNECTED
  │     │     └─ ota_event_handler() → publish_status(client, "CONNECTED")
  │     └─ THAT BAI (khong co event nao) → publish_status(client, "HTTPS_CONNECT_FAILED") → goto cleanup
  │
  ├─ esp_https_ota_get_img_desc(handle, &app_desc)
  │     └─ dispatch ESP_HTTPS_OTA_GET_IMG_DESC → publish_status("READING_IMG_DESC")
  │
  ├─ while esp_https_ota_perform(handle) == ESP_ERR_HTTPS_OTA_IN_PROGRESS:
  │     └─ (lan dau) dispatch VERIFY_CHIP_ID, VERIFY_CHIP_REVISION
  │     └─ (moi vong lap) dispatch WRITE_FLASH → publish_status("WRITING_FLASH", so_byte)
  │
  ├─ esp_https_ota_is_complete_data_received(handle)? → khong thi goto cleanup
  │
  ├─ esp_https_ota_finish(handle)
  │     ├─ THANH CONG (verify chu ky OK)
  │     │     └─ dispatch UPDATE_BOOT_PARTITION → publish_status("BOOT_PARTITION_UPDATED")
  │     │     └─ dispatch FINISH → publish_status("OTA_FINISH")
  │     │     └─ publish_status("REBOOTING") → esp_restart()   [KHONG BAO GIO CHAY TIEP]
  │     └─ THAT BAI (vd sai chu ky)
  │           └─ publish_status("SIGNATURE_INVALID") hoac loi khac → xuong cleanup
  │
  └─ cleanup:
        ├─ esp_event_handler_unregister(ESP_HTTPS_OTA_EVENT, ...)
        ├─ free(url)
        ├─ ► ota_manager_notify_task_done()                     [ota_manager.c]  (reset s_ota_in_progress = false)
        └─ vTaskDelete(NULL)
```

### D. Sơ đồ tổng thể — 4 điểm nối chính (chính là 4 hàm cần gọi khi porting ở mục 7)

```
                    ┌─────────────────────────────────────────┐
app_main() ────────►│ ④  ota_rollback_start_confirm_watchdog() │  NGAY LUC BOOT, truoc wifi_manager_start()
                    │                                           │
main.c (MQTT) ─────►│ ①  ota_manager_start(client)             │  khi MQTT connect
                    │ ②  ota_manager_handle_mqtt_data(event)   │  khi co ban tin MQTT
                    │ ③  ota_rollback_confirm_if_pending()     │  tai checkpoint suc khoe
                    └─────────────────────────────────────────┘
                                      │
                                      ▼
                    components/ota_manager/  (tu quan ly toan bo phan con lai:
                                                guard, tai file, verify chu ky,
                                                publish status, rollback)
```

Phần hướng dẫn porting bên dưới sẽ chỉ đúng vào 4 điểm nối ① ② ③ ④ này. ④ mới thêm (xem `OTA_PLAN.md` mục "Đồng hồ cảnh báo xác nhận rollback") — chống thiết bị treo vô thời hạn nếu WiFi/MQTT không bao giờ kết nối được sau OTA.

---

## 0. Phạm vi port — copy gì, KHÔNG copy gì

| | Có copy không? |
|---|---|
| `components/ota_manager/` (toàn bộ) | ✅ Copy nguyên |
| `components/wifi_manager/` | ❌ **TUYỆT ĐỐI KHÔNG** — project mới đã có WiFi riêng, xem mục cảnh báo cuối bài |
| `main/main.c` (project mẫu) | ❌ Không copy file — chỉ tham khảo cách gọi 4 hàm (mục 7) |
| `version.txt` | ⚠️ Tạo file mới ở project mới, không copy nội dung |
| `ota_https_server/` (Node.js server) | ✅ Có thể copy nguyên nếu chưa có hạ tầng OTA server riêng |

---

## 1. Copy component

```bash
cp -r /home/vietnq/esp/ota/components/ota_manager /path/to/new_project/components/
```

Kết quả trong project mới:
```
new_project/components/ota_manager/
├── CMakeLists.txt
├── Kconfig                ← dinh nghia cac option menuconfig cua component (xem muc 4)
├── include/
│   ├── ota_manager.h
│   ├── ota_rollback.h
│   └── ota_task.h
├── ota_manager.c
├── ota_rollback.c
├── ota_task.c
└── server_certs/
    └── ca_cert.pem        ← PLACEHOLDER, phải thay bằng cert thật (xem mục 8)
```

`Kconfig` **không cần khai báo/include gì thêm** ở project mới — ESP-IDF tự quét file `Kconfig` trong mọi component khi chạy `idf.py menuconfig`/`reconfigure`, chỉ cần nằm đúng vị trí `components/ota_manager/Kconfig` là tự xuất hiện trong menu (xem giải thích cơ chế Kconfig → sdkconfig → sdkconfig.json ở phần trao đổi trước nếu cần hiểu sâu hơn).

## 2. Tạo `version.txt` ở gốc project mới

```bash
echo "1.0.0" > /path/to/new_project/version.txt
```

Nếu project mới **đã có** cơ chế versioning riêng (vd `CONFIG_APP_PROJECT_VER` qua menuconfig, hoặc tự set `PROJECT_VER` trong `CMakeLists.txt`), **không tạo file này** — giữ nguyên cơ chế cũ, chỉ cần đảm bảo `esp_app_get_description()->version` (dùng trong `ota_manager.c`) trả về đúng giá trị mong muốn, không quan trọng version đó lấy từ nguồn nào.

## 3. Cập nhật `main/CMakeLists.txt` của project mới

Chỉ cần thêm `ota_manager` vào `REQUIRES` — component tự kéo theo `esp_https_ota`/`json`/`app_update`/`esp_event` qua `PRIV_REQUIRES` của chính nó, không cần khai báo lại ở main:

```cmake
idf_component_register(SRCS "main.c"          # ten file cua ban, co the khac
                    INCLUDE_DIRS "."
                    REQUIRES ... mqtt ota_manager ...)   # them "ota_manager"
```

> `mqtt` phải có trong `REQUIRES` — nếu project đã dùng MQTT sẵn thì chắc chắn đã có, không cần thêm.

## 4. menuconfig (bắt buộc trước khi build lần đầu)

Chi tiết đầy đủ + giải thích tại `RETROFIT_OTA_ESP32C5.md` mục 2-4. Tóm tắt các mục cần bật:

```
idf.py menuconfig
→ Partition Table → Custom partition table CSV (2 OTA slot, xem thiết kế riêng cho board của bạn)
→ Bootloader config → Enable app rollback support
→ Security features → Require signed app images
                     → App Signing Scheme → ⚠️ XEM CẢNH BÁO NGAY BÊN DƯỚI, KHÔNG bấm mặc định
                     → Verify app signature on update
                     → Sign binaries during build
                     → Secure boot private signing key = <đường dẫn khóa đã tạo>
→ Component config → ESP System Settings → Task Watchdog timeout period (seconds) → 15
```

⚠️ **"App Signing Scheme" PHỤ THUỘC CHIP đích, không có giá trị mặc định đúng cho mọi target:**
- ESP32 (Xtensa) gốc — project mẫu `esp/ota` dùng **`ECDSA` (Secure Boot V1)**, tạo khóa bằng `espsecure.py generate_signing_key --version 1 <file>.pem`.
- ESP32-C5 (RISC-V) — **KHÔNG hỗ trợ** scheme ECDSA-V1 ở trên (đã verify trực tiếp trong Kconfig gốc ESP-IDF: option đó `depends on SECURE_BOOT_V1_SUPPORTED`, chip này không có). Chỉ chọn được `RSA` hoặc `ECDSA (V2)`, tạo khóa bằng `espsecure.py generate_signing_key --version 2 --scheme <rsa3072|ecdsa192|ecdsa256|ecdsa384> <file>.pem`. Scheme `RSA` còn có thêm ràng buộc riêng theo chip revision (`ESP32C5_REV_MIN_FULL`) — xem hướng dẫn chi tiết từng bước (cách xác nhận chip revision, cách chọn scheme, cách tạo khóa đúng) tại `RETROFIT_OTA_ESP32C5.md` mục 4.

Chọn nhầm scheme (vd copy nguyên lệnh `--version 1` từ project mẫu sang project C5) sẽ khiến build lỗi hoặc verify chữ ký sai ngay từ lần OTA đầu tiên — đây là chỗ khác biệt LỚN nhất giữa port sang ESP32 gốc và port sang ESP32-C5, không phải chi tiết nhỏ có thể bỏ qua.

Tùy chọn (mặc định TẮT, không cần đụng vào nếu chỉ dùng HTTPS): `Component config → OTA Manager → Allow OTA over plain HTTP` + `Component config → ESP HTTPS OTA → Allow HTTP for OTA` — xem mục "Chuyển đổi giữa HTTP và HTTPS" trong `OTA_PLAN.md` nếu project mới cần fallback sang HTTP lúc dev/test. ⚠️ Đây là `#if` biên dịch có điều kiện — đổi trong menuconfig xong **phải build lại và flash lại** mới có hiệu lực, sửa `sdkconfig` không đủ.

## 5. Tùy chỉnh topic MQTT trước khi build (khuyến nghị)

`OTA_COMMAND_TOPIC` và `OTA_STATUS_TOPIC` hiện đang là **hằng số cấu hình lúc build** (không phải Kconfig, không phải runtime), khai báo cứng trong [`include/ota_manager.h`](../components/ota_manager/include/ota_manager.h):

```c
#define OTA_COMMAND_TOPIC "esp32/vietnq/ota/cmd"
#define OTA_STATUS_TOPIC  "esp32/vietnq/ota/status"
```

Đây là topic đặt riêng cho project mẫu (`esp32/vietnq/...`) — **gần như chắc chắn bạn muốn đổi** trước khi dùng ở project mới, để tránh trùng namespace với project mẫu (cùng broker công cộng `broker.hivemq.com` thì 2 project khác nhau vẫn có thể vô tình đụng topic nhau). Sửa trực tiếp 2 dòng `#define` này trong `ota_manager.h`, ví dụ đặt theo tên project/device ID riêng, rồi `idf.py build` lại — không cần sửa gì thêm ở `ota_manager.c`/`ota_task.c` vì cả 2 file đều dùng qua tên macro, không hardcode chuỗi topic ở nơi khác.

⚠️ **Lưu ý khi test lần đầu — MQTT retained message:** nếu bạn (hoặc ai khác) từng publish lệnh OTA lên đúng topic này **kèm cờ `retain`**, broker sẽ tự động phát lại y nguyên bản tin đó cho **bất kỳ thiết bị nào** subscribe sau đó — kể cả thiết bị vừa mới port/flash xong, dù không ai chủ động gửi lại. Đã gặp thật khi test project mẫu. Trước khi test lần đầu ở project mới, nên xóa sạch retained message cũ trên topic lệnh (nếu topic từng được dùng trước đó):
```bash
mosquitto_pub -h <broker> -t "<OTA_COMMAND_TOPIC_moi>" -r -n
```

## 6. Include header vào file chứa MQTT event handler hiện tại

```c
#include "ota_manager.h"
#include "ota_rollback.h"
```

## 7. 4 điểm gọi hàm — chèn vào ĐÚNG vị trí trong code MQTT hiện có

### 7.1 `ota_manager_start(esp_mqtt_client_handle_t client)`

- **Gọi ở đâu:** trong `case MQTT_EVENT_CONNECTED` của MQTT event handler hiện tại của bạn, sau khi đã có `client` handle.
- **Tham số:** `client` — chính `esp_mqtt_client_handle_t` bạn đang lấy từ `event->client` (hoặc biến toàn cục bạn đang giữ sẵn từ lúc `esp_mqtt_client_init()`, nếu code hiện tại lưu theo cách đó).
- **An toàn gọi lại nhiều lần** — mỗi lần MQTT reconnect gọi lại vẫn OK, chỉ subscribe lại đúng 1 topic, không side-effect gì thêm.

### 7.2 `ota_manager_handle_mqtt_data(const esp_mqtt_event_handle_t event)`

- **Gọi ở đâu:** trong `case MQTT_EVENT_DATA`, bất kỳ vị trí nào trong case đó (trước/sau code xử lý topic cũ của bạn đều được).
- **Tham số:** `event` — chính object event gốc esp-mqtt đưa vào callback, **không cần parse/chỉnh sửa gì trước**.
- Hàm tự kiểm tra `event->topic` có khớp `OTA_COMMAND_TOPIC` (`esp32/vietnq/ota/cmd`) không — không khớp thì tự bỏ qua ngay, không đụng gì tới xử lý topic khác của bạn.

### 7.3 `ota_rollback_confirm_if_pending(void)`

- **Gọi ở đâu:** tại 1 "checkpoint sức khỏe" do **bạn tự chọn** — nơi bạn tin chắc firmware mới đã chạy đúng. Ví dụ trong đoạn mẫu: ngay khi MQTT connect thành công (giống 6.1). Có thể chọn checkpoint khác phù hợp hơn với nghiệp vụ của bạn (vd sau khi đọc cảm biến lần đầu OK, sau khi kết nối 1 server backend riêng thành công...).
- **Tham số:** không có (`void`).
- Hàm tự có cờ nội bộ, gọi lặp lại bao nhiêu lần cũng chỉ thực thi đúng 1 lần — không cần tự viết thêm guard bên ngoài.

### 7.4 `ota_rollback_start_confirm_watchdog(uint32_t timeout_ms)`

- **Gọi ở đâu:** trong `app_main()`, **NGAY LÚC BOOT**, **TRƯỚC** khi bắt đầu kết nối WiFi (trước `wifi_manager_start()` hoặc tương đương) — để đồng hồ tính đủ cả thời gian chờ WiFi retry lẫn MQTT connect. **KHÔNG** đặt trong `mqtt_event_handler` như 3 hàm còn lại.
- **Tham số:** `timeout_ms` — thời hạn tối đa (mili-giây) chờ checkpoint (7.3) được xác nhận, kể từ lúc gọi hàm này. Project mẫu dùng `2 * 60 * 1000` (2 phút) — chỉ là điểm khởi đầu hợp lý, **chưa kiểm chứng bằng test thật**, cần cân nhắc lại theo đặc thù mạng của bạn.
- **Vì sao cần:** nếu không có hàm này, thiết bị có thể **treo vô thời hạn** ở trạng thái `PENDING_VERIFY` nếu firmware OTA mới có bug khiến WiFi/MQTT không bao giờ kết nối được (nhưng bản thân app không crash) — xem giải thích đầy đủ tại `OTA_PLAN.md` mục "Đồng hồ cảnh báo xác nhận rollback". Hàm tự kiểm tra có đang `PENDING_VERIFY` hay không trước khi tạo timer, và tự huỷ nếu 7.3 confirm thành công trước hạn — an toàn gọi vô điều kiện ở mọi lần boot, không chỉ sau OTA.

### Ví dụ before/after cụ thể (dựa đúng code thật trong `main/main.c` của project mẫu)

```c
void app_main(void)
{
    // ... nvs_flash_init() GIU NGUYEN ...

    ota_rollback_start_confirm_watchdog(2 * 60 * 1000);   // MOI - them dong nay, TRUOC wifi_manager_start()

    wifi_manager_start();   // hoac tuong duong cua ban
    // ...
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED:
        // ... code nghiep vu HIEN TAI cua ban (subscribe topic cu, log, publish trang thai...) GIU NGUYEN ...

        ota_manager_start(client);                 // MOI - them dong nay
        ota_rollback_confirm_if_pending();         // MOI - them dong nay
        break;

    case MQTT_EVENT_DATA:
        // ... code nghiep vu HIEN TAI cua ban (xu ly cac topic khac) GIU NGUYEN ...

        ota_manager_handle_mqtt_data(event);       // MOI - them dong nay
        break;

    // ... cac case khac (DISCONNECTED, ERROR...) GIU NGUYEN, khong can sua gi ...
    }
}
```

## 8. Thay cert placeholder bằng cert thật

File copy ở Bước 1 (`server_certs/ca_cert.pem`) chỉ là placeholder, **không dùng được**. Tạo và thay bằng cert HTTPS server thật — xem `RETROFIT_OTA_ESP32C5.md` (mục Secure OTA) hoặc `OTA_PLAN.md` Bước 6 cho cách tạo chi tiết.

## 9. Build thử

```bash
idf.py set-target esp32c5
idf.py build
```

Nếu lỗi thiếu component (`esp_https_ota`, `json`, `app_update`, `esp_timer`...) — các thành phần này đã khai báo sẵn trong `PRIV_REQUIRES` của `components/ota_manager/CMakeLists.txt`, **không cần khai báo lại ở main**. Nếu vẫn lỗi, kiểm tra lại `ota_manager` đã nằm đúng vị trí `<project_root>/components/ota_manager/` (ngang hàng thư mục `main/`) chưa.

---

## Bảng tra nhanh — toàn bộ API cần gọi từ code project mới

| Hàm | Khai báo trong | Gọi ở đâu | Tham số | Trả về |
|---|---|---|---|---|
| `ota_manager_start` | `ota_manager.h` | `case MQTT_EVENT_CONNECTED` | `esp_mqtt_client_handle_t client` | `void` |
| `ota_manager_handle_mqtt_data` | `ota_manager.h` | `case MQTT_EVENT_DATA` | `const esp_mqtt_event_handle_t event` | `void` |
| `ota_rollback_confirm_if_pending` | `ota_rollback.h` | tại checkpoint bạn chọn | *(không có)* | `void` |
| `ota_rollback_start_confirm_watchdog` | `ota_rollback.h` | `app_main()`, TRƯỚC `wifi_manager_start()` | `uint32_t timeout_ms` | `void` |

`ota_manager_notify_task_done()` và toàn bộ nội dung `ota_task.c`/`.h` là **nội bộ (internal)** — `ota_manager.c` tự gọi, code project mới **không cần và không nên** gọi trực tiếp.

---

## ⚠️ TUYỆT ĐỐI KHÔNG COPY

- **`components/wifi_manager/`** — project mới đã có WiFi chạy sẵn. Copy component này vào sẽ khiến `esp_wifi_init()`/`esp_netif_create_default_wifi_sta()` bị gọi **lần thứ 2**, xung đột với code WiFi hiện tại (có thể lỗi build, hoặc tệ hơn là lỗi runtime khó debug), đồng thời đè SSID/password hardcode sai của project mẫu lên project của bạn.
- **`main/main.c` của project mẫu** — chỉ dùng để tham khảo cách gọi 4 hàm ở mục 7, **không copy nguyên file** — `main.c` của project mới phải giữ nguyên toàn bộ nghiệp vụ hiện tại, chỉ thêm đúng 4 dòng gọi hàm + 2 dòng include.

---

## Điều kiện ngầm định cần tự verify trước khi port (không phải lỗi, nhưng là phụ thuộc ẩn)

1. **Project mới đã gọi `esp_event_loop_create_default()` từ trước** — `ota_task.c` đăng ký lắng nghe `ESP_HTTPS_OTA_EVENT` trên **default event loop**, không tự tạo loop riêng. Vì project đã có WiFi/MQTT chạy, gần như chắc chắn loop này đã tồn tại — chỉ cần xác nhận, không cần thêm gì nếu đúng vậy.
2. **Project mới dùng đúng thư viện `esp-mqtt` chuẩn ESP-IDF** (kiểu `esp_mqtt_client_handle_t`/`esp_mqtt_event_handle_t`) — nếu dùng thư viện MQTT khác, phải viết lại phần ký hiệu tham số trong `ota_manager.h`/`ota_manager.c` cho khớp.
3. **Rà lại code nghiệp vụ hiện tại** xem có chỗ nào kiểm tra đang chạy từ partition `factory` không (vd logic factory-reset đặc biệt) — bảng partition mới (theo thiết kế trong `RETROFIT_OTA_ESP32C5.md`) không còn `factory`.

## Sau khi port xong — vẫn cần vá trước khi dùng thật (lỗi có sẵn trong `ota_manager.c`, không phát sinh do việc port)

- `is_version_newer()` trong `ota_manager.c` chỉ `strcmp` khác nhau, chưa chặn được downgrade — xem `KNOWN_ISSUES.md` mục 1.
- `OTA_COMMAND_TOPIC` chưa có xác thực nếu broker vẫn là broker công cộng/ẩn danh — xem `KNOWN_ISSUES.md` mục 1.
