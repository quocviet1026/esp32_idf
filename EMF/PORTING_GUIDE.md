# Hướng dẫn Porting `ota_manager` vào project EMF (ESP32-C5)

> Bản dành riêng cho project EMF (tên nội bộ trên thiết bị: "ESL"). **Nguồn copy: `/home/vietnq/esp/esp32c5_rak3172`** — project ESP32-C5 đã build sạch, đã wire OTA thật vào `components/transport/transport_wifi_mqtt.c` (KHÔNG lấy từ `/home/vietnq/esp/ota`, project ESP32 Xtensa gốc — component `ota_manager` bắt nguồn từ đó nhưng bản ở `esp32c5_rak3172` đã tiến hoá thêm, xem Mục "Vì sao chọn nguồn này" bên dưới). Hướng dẫn từng bước **cụ thể** (gọi hàm nào, ở đâu, tham số gì).
>
> **Đọc theo đúng thứ tự với 2 tài liệu khác trong thư mục này:**
> 1. `RESTRUCTURE_TRANSPORT_ABSTRACTION.md` — làm TRƯỚC (nếu EMF chưa có transport abstraction). Sau khi xong, MQTT event handler của EMF nằm trong file implementation WiFi/MQTT mới (kiểu `transport_wifi_mqtt.c`, đúng theo mẫu `esp32c5_rak3172`) thay vì rải rác trong `app_main()`/file cũ.
> 2. `RETROFIT_OTA_ESP32C5.md` — kiến trúc tổng quan (partition table, Secure OTA V2, rollback watchdog). File hiện tại (`PORTING_GUIDE.md`) chỉ tập trung vào **thao tác code cụ thể** để hiện thực hoá kiến trúc đó.

## Vì sao chọn `esp32c5_rak3172` làm nguồn, không phải `esp/ota`

`esp/ota` (ESP32 Xtensa) là nơi component `ota_manager` được viết ra đầu tiên và test thật đầy đủ nhất (rollback thật, ký sai key thật...). Nhưng `esp32c5_rak3172` đã copy nguyên component đó, build sạch cho đúng **target `esp32c5`**, và bổ sung thêm:
- `ota_manager_is_busy()` — hàm mới, không có trong bản gốc `esp/ota` (thêm sau này để phục vụ tính năng sleep mode ở project khác, nhưng hữu ích chung: báo "đang có OTA active hay không" cho bất kỳ phần nào của firmware cần biết trước khi làm việc gì có thể bị OTA làm gián đoạn).
- Đã wire theo kiến trúc **dispatch table** cho `MQTT_EVENT_DATA` (mạnh hơn cách gọi trực tiếp không điều kiện trong `case MQTT_EVENT_DATA` như bản `esp/ota` gốc) — xem Mục 7.2.

Vì EMF là target ESP32-C5 giống hệt `esp32c5_rak3172`, dùng đúng bản đã tiến hoá + build-tested trên cùng target là lựa chọn an toàn hơn quay lại bản Xtensa gốc.

---

## Tổng quan luồng chạy (Runtime Flow) — theo đúng code thật trong `esp32c5_rak3172`

### A. Luồng khởi động (boot) — tới lúc sẵn sàng nhận lệnh OTA

```
app_main()                                                    [main.c]
  ├─ log_partition_table(), nvs_flash_init(), cli_console_start(), app_config_load()
  └─ s_transport->start(&cfg)   // = wifi_mqtt_start(), xem transport_wifi_mqtt.c

wifi_mqtt_start(cfg)                                          [transport_wifi_mqtt.c]
  │
  ├─ ► ota_rollback_start_confirm_watchdog(ROLLBACK_CONFIRM_TIMEOUT_MS)  ← DIEM NOI #4
  │       (goi TRUOC wifi_manager_start(), 2*60*1000 = 2 phut)
  │       └─ neu dang PENDING_VERIFY: dat hen gio, qua han ma chua confirm
  │          se CHU DONG esp_ota_mark_app_invalid_rollback_and_reboot()
  │
  ├─ wifi_manager_start(cfg->wifi_ssid, cfg->wifi_pass, ...)
  └─ esp_event_handler_register(IP_EVENT_STA_GOT_IP, ip_event_handler)

... (bat dong bo, cho WiFi co IP) ...

ip_event_handler(IP_EVENT_STA_GOT_IP) → mqtt_start() → esp_mqtt_client_init()+start()

... (bat dong bo, cho MQTT connect) ...

mqtt_event_handler(MQTT_EVENT_CONNECTED)                      [transport_wifi_mqtt.c]
  ├─ publish version len VERSION_TOPIC
  ├─ esp_mqtt_client_subscribe(client, COMMAND_TOPIC, 1)   (topic lenh rieng cua app, KHAC OTA_COMMAND_TOPIC)
  ├─ ► ota_manager_start(client)                            ← DIEM NOI #1
  │       └─ esp_mqtt_client_subscribe(client, OTA_COMMAND_TOPIC)
  ├─ ► ota_rollback_confirm_if_pending()                    ← DIEM NOI #3
  │       └─ neu state == PENDING_VERIFY → esp_ota_mark_app_valid_cancel_rollback()
  └─ transport_notify_ready()
```

### B. Luồng nhận lệnh trigger OTA qua MQTT — dùng dispatch table, KHÔNG gọi vô điều kiện

```
Server publish {"version":"x.y.z","url":"https://..."} len OTA_COMMAND_TOPIC
        │
mqtt_event_handler(MQTT_EVENT_DATA) → mqtt_dispatch_data(event)   [transport_wifi_mqtt.c]
  │
  │  static const mqtt_topic_route_t s_mqtt_routes[] = {
  │      { OTA_COMMAND_TOPIC, ota_manager_handle_mqtt_data },   ← DIEM NOI #2 (dang ky trong bang, khong goi truc tiep)
  │      { COMMAND_TOPIC,     handle_command_topic },
  │  };
  │
  └─ so sanh event->topic voi tung dong trong bang, khop dong nao goi DUNG handler do
        │
        ▼ (neu khop OTA_COMMAND_TOPIC)
ota_manager_handle_mqtt_data(event)                            [ota_manager.c]
        ├─ parse JSON (cJSON)
        ├─ is_version_newer(version)?  → so voi esp_ota_get_running_partition()
        ├─ dang co s_ota_in_progress (ota_manager_is_busy())?  → neu co, bo qua
        └─ neu hop le → s_ota_in_progress = true → ota_task_start(client, url)
```

**Vì sao dùng dispatch table thay vì gọi `ota_manager_handle_mqtt_data(event)` vô điều kiện trong mọi `MQTT_EVENT_DATA`** (như bản `esp/ota` gốc làm): khi EMF có nhiều topic khác nhau (lệnh nghiệp vụ riêng, OTA...), so sánh topic 1 lần trong bảng rồi gọi đúng 1 handler tránh việc mỗi hàm xử lý phải tự `strncmp` lại từ đầu, và khi bài tin tới trên topic không ai đăng ký sẽ được LOG RÕ RÀNG (`"Khong co handler nao dang ky cho topic..."`) thay vì âm thầm biến mất.

### C. Luồng thực thi task OTA (không đổi so với `esp/ota` gốc — `ota_task.c` giống hệt, xem diff xác nhận ở cuối tài liệu)

```
ota_task(pvParameter)                                          [ota_task.c]
  ├─ esp_https_ota_begin() → esp_https_ota_get_img_desc() → esp_https_ota_perform() (vong lap)
  ├─ esp_https_ota_finish()
  │     ├─ THANH CONG → publish_status("BOOT_PARTITION_UPDATED"/"OTA_FINISH"/"REBOOTING") → esp_restart()
  │     └─ THAT BAI (vd sai chu ky) → publish_status("SIGNATURE_INVALID") → cleanup
  └─ cleanup: ► ota_manager_notify_task_done()   (reset s_ota_in_progress = false)
```

### D. Sơ đồ tổng thể

```
                    ┌──────────────────────────────────────────┐
main.c ────────────►│ (khong goi truc tiep gi ca - chi transport│
                    │  ->start() duy nhat)                      │
                    └──────────────────────────────────────────┘
                                      │
                                      ▼
transport_wifi_mqtt.c (wifi_mqtt_start) ►│ ④ ota_rollback_start_confirm_watchdog() │  TRUOC wifi_manager_start()
transport_wifi_mqtt.c (mqtt_event_handler,
  MQTT_EVENT_CONNECTED)              ►│ ①  ota_manager_start(client)             │
                                      │ ③  ota_rollback_confirm_if_pending()     │
transport_wifi_mqtt.c (s_mqtt_routes[])►│ ②  ota_manager_handle_mqtt_data (dang ky trong bang) │
                                      └──────────────────────────────────────────┘
                                      │
                                      ▼
                    components/ota_manager/  (tu quan ly toan bo phan con lai)
```

**Với EMF:** nếu đã áp dụng `RESTRUCTURE_TRANSPORT_ABSTRACTION.md`, 4 điểm nối trên chèn vào ĐÚNG file implementation WiFi/MQTT mới của EMF (file tương đương `transport_wifi_mqtt.c`). Nếu EMF vẫn giữ `app_main()`/1 file MQTT handler phẳng, `④` chèn ngay đầu `app_main()` (trước khi bắt đầu kết nối WiFi), `①③` chèn trong `case MQTT_EVENT_CONNECTED`, `②` chèn trong `case MQTT_EVENT_DATA` (có thể gọi trực tiếp hoặc theo dispatch table tuỳ EMF đã có sẵn cơ chế đó chưa).

---

## 0. Phạm vi port — copy gì, KHÔNG copy gì

| | Có copy không? |
|---|---|
| `components/ota_manager/` (toàn bộ, từ `esp32c5_rak3172`) | ✅ Copy nguyên |
| `components/wifi_manager/` (từ `esp32c5_rak3172`) | ❌ **TUYỆT ĐỐI KHÔNG** — EMF đã có driver WiFi riêng, xem mục cảnh báo cuối bài |
| `components/transport/` (từ `esp32c5_rak3172`) | ❌ KHÔNG copy nguyên — đây là code THAM KHẢO cách wire 4 điểm nối, EMF tự chèn vào file MQTT handler của mình (xem `RESTRUCTURE_TRANSPORT_ABSTRACTION.md` nếu muốn áp dụng đúng kiến trúc này) |
| `main/main.c` (từ `esp32c5_rak3172`) | ❌ Không copy file — chỉ tham khảo thứ tự gọi ở Mục A |
| `version.txt` | ⚠️ Xem Mục 2 — EMF có thể đã có cơ chế riêng |

---

## 1. Copy component

```bash
cp -r /home/vietnq/esp/esp32c5_rak3172/components/ota_manager <đường_dẫn_source_EMF>/components/
```

Kết quả:
```
components/ota_manager/
├── CMakeLists.txt
├── Kconfig
├── include/
│   ├── ota_manager.h      ← co them ota_manager_is_busy(), khac ban esp/ota goc
│   ├── ota_rollback.h
│   └── ota_task.h
├── ota_manager.c          ← co them ham ota_manager_is_busy()
├── ota_rollback.c
├── ota_task.c
└── server_certs/
    └── ca_cert.pem        ← PLACEHOLDER cert that cua esp32c5_rak3172, PHAI thay bang cert that cua EMF (xem muc 8)
```

`Kconfig` không cần khai báo/include gì thêm — ESP-IDF tự quét, chỉ cần nằm đúng `components/ota_manager/Kconfig`.

## 2. `version.txt`

```bash
echo "1.0.0" > <đường_dẫn_source_EMF>/version.txt
```

**Kiểm tra trước:** app image hiện tại của EMF đã có version dạng `v1.0.0-1-g0bd2023` (xác nhận qua `esptool.py image_info`, xem `FLASH_ANALYSIS.md`) — tức **EMF đã có cơ chế versioning riêng** (git-describe qua `PROJECT_VER`, không phải file `version.txt` tĩnh). **KHÔNG tạo `version.txt`** trong trường hợp này — chỉ cần đảm bảo `esp_app_get_description()->version` (dùng trong `ota_manager.c`) trả về đúng giá trị mong muốn.

## 3. Cập nhật `main/CMakeLists.txt`

```cmake
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES ... mqtt ota_manager ...)   # them "ota_manager"
```

Component tự kéo theo `esp_https_ota`/`json`/`app_update`/`esp_event`/`esp_timer` qua `PRIV_REQUIRES` của chính nó (xem `components/ota_manager/CMakeLists.txt`), không cần khai báo lại.

## 4. menuconfig (bắt buộc trước khi build lần đầu)

Chi tiết đầy đủ tại `RETROFIT_OTA_ESP32C5.md` mục 2-4. Tóm tắt:

```
idf.py menuconfig
→ Partition Table → Custom partition table CSV (dùng /home/vietnq/esp/EMF/partitions.csv đã thiết kế sẵn theo số liệu thật của EMF)
→ Serial flasher config → Flash size → 8 MB (khớp số đo thật, xem FLASH_ANALYSIS.md)
→ Bootloader config → Enable app rollback support
→ Security features → Require signed app images
                     → App Signing Scheme → ⚠️ XEM CẢNH BÁO NGAY BÊN DƯỚI
                     → Verify app signature on update
                     → Sign binaries during build
                     → Secure boot private signing key = <đường dẫn khóa đã tạo>
→ Component config → ESP System Settings → Task Watchdog timeout period (seconds) → 15
```

⚠️ **`esp32c5_rak3172` (nguồn copy) CHƯA bật Secure Boot signing** (chưa tạo key, chưa test) — EMF sẽ là nơi ĐẦU TIÊN thực sự làm bước này cho hardware family ESP32-C5 trong các project hiện có. Không copy được sẵn key/config từ đâu — phải tự làm mới theo đúng `RETROFIT_OTA_ESP32C5.md` mục 4:
- ESP32-C5 (RISC-V) **không hỗ trợ** scheme `ECDSA` (Secure Boot V1) mà `esp/ota` (ESP32 Xtensa) dùng — verify trực tiếp trong Kconfig gốc ESP-IDF (option đó `depends on SECURE_BOOT_V1_SUPPORTED`, chip C5 không có).
- Chỉ chọn được `RSA` hoặc `ECDSA (V2)`. Tạo khóa: `espsecure.py generate_signing_key --version 2 --scheme <rsa3072|ecdsa192|ecdsa256|ecdsa384> <file>.pem`.
- Scheme `RSA` có ràng buộc riêng theo chip revision — **tự kiểm tra chip revision thật của board EMF** (`espefuse.py summary`) trước khi chọn, không đoán.

Tùy chọn (mặc định TẮT): `Component config → OTA Manager → Allow OTA over plain HTTP` + `Component config → ESP HTTPS OTA → Allow HTTP for OTA` — chỉ bật nếu EMF cần fallback HTTP lúc dev/test.

## 5. Tùy chỉnh topic MQTT trước khi build

```c
// components/ota_manager/include/ota_manager.h - hien dang mang ten esp32c5_rak3172
#define OTA_COMMAND_TOPIC "esp32c5_rak3172/vietnq/ota/cmd"
#define OTA_STATUS_TOPIC  "esp32c5_rak3172/vietnq/ota/status"
```

Đổi sang namespace MQTT topic **EMF đang thực sự dùng** (tìm được ở Mục 0 của `RESTRUCTURE_TRANSPORT_ABSTRACTION.md` khi audit code EMF thật) — không giữ nguyên `esp32c5_rak3172/vietnq/...` của project nguồn.

⚠️ **MQTT retained message:** nếu topic từng được publish kèm cờ `retain` (kể cả từ project khác cùng broker), broker sẽ tự phát lại y nguyên bản tin đó cho thiết bị EMF vừa port/flash xong. Xoá sạch trước khi test lần đầu:
```bash
mosquitto_pub -h <broker> -t "<OTA_COMMAND_TOPIC_moi>" -r -n
```

## 6. Include header

```c
#include "ota_manager.h"
#include "ota_rollback.h"
```

Thêm vào đúng file chứa MQTT event handler của EMF (sau khi restructure: `transport_wifi_mqtt.c` hoặc tên tương đương).

## 7. 4 điểm gọi hàm

### 7.1 `ota_manager_start(esp_mqtt_client_handle_t client)`
Gọi trong `case MQTT_EVENT_CONNECTED`, sau khi có `client` handle. An toàn gọi lại mỗi lần MQTT reconnect.

### 7.2 `ota_manager_handle_mqtt_data(const esp_mqtt_event_handle_t event)`
Nếu EMF đã có (hoặc sẽ làm theo `RESTRUCTURE_TRANSPORT_ABSTRACTION.md`) 1 dispatch table cho `MQTT_EVENT_DATA` — thêm 1 dòng `{ OTA_COMMAND_TOPIC, ota_manager_handle_mqtt_data }` vào bảng đó, đúng pattern thật trong `transport_wifi_mqtt.c` (Mục B ở trên). Nếu EMF chưa có dispatch table, gọi trực tiếp trong `case MQTT_EVENT_DATA` — hàm tự kiểm tra topic khớp không, không khớp tự bỏ qua, an toàn gọi vô điều kiện.

### 7.3 `ota_rollback_confirm_if_pending(void)`
Gọi tại 1 checkpoint sức khỏe EMF tự chọn (đơn giản nhất: ngay khi MQTT connect thành công, giống 7.1). Có cờ nội bộ, gọi lặp lại vẫn chỉ chạy đúng 1 lần.

### 7.4 `ota_rollback_start_confirm_watchdog(uint32_t timeout_ms)`
Gọi **NGAY LÚC BẮT ĐẦU KẾT NỐI MẠNG** (đúng vị trí thật trong `esp32c5_rak3172`: đầu hàm `start()` của transport WiFi/MQTT, TRƯỚC dòng gọi driver WiFi) — **KHÔNG** đặt trong MQTT event handler như 3 hàm còn lại. `timeout_ms` dùng `2 * 60 * 1000` (2 phút) trong project nguồn — cần đo lại theo thời gian WiFi/MQTT connect thật của EMF trước khi tin tưởng cho production.

### Ví dụ before/after (theo đúng code thật `transport_wifi_mqtt.c`)

```c
static esp_err_t wifi_mqtt_start(const app_config_t *cfg)   // hoac ham tuong duong cua EMF
{
    ota_rollback_start_confirm_watchdog(2 * 60 * 1000);   // MOI - TRUOC ca wifi_manager_start()

    wifi_manager_start(cfg->wifi_ssid, cfg->wifi_pass, NULL);
    return esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED:
        // ... code nghiep vu HIEN TAI cua EMF GIU NGUYEN ...

        ota_manager_start(client);                 // MOI
        ota_rollback_confirm_if_pending();         // MOI
        break;

    case MQTT_EVENT_DATA:
        // ... code nghiep vu HIEN TAI cua EMF GIU NGUYEN ...
        // (hoac them 1 dong vao dispatch table neu EMF da co, xem Muc 7.2)

        ota_manager_handle_mqtt_data(event);       // MOI (neu goi truc tiep, khong qua dispatch table)
        break;

    // ... cac case khac GIU NGUYEN ...
    }
}
```

## 8. Thay cert placeholder bằng cert thật

File copy ở Bước 1 (`server_certs/ca_cert.pem`) là cert thật của HTTPS OTA server dùng cho `esp32c5_rak3172` — **không dùng được cho EMF**. Tạo và thay bằng cert HTTPS server thật của EMF — xem `RETROFIT_OTA_ESP32C5.md` mục 4 (Secure OTA).

## 9. Build thử

```bash
idf.py set-target esp32c5
idf.py build
```

Nếu lỗi thiếu component (`esp_https_ota`, `json`, `app_update`, `esp_timer`...) — đã khai báo sẵn trong `PRIV_REQUIRES` của `components/ota_manager/CMakeLists.txt`. Kiểm tra `ota_manager` nằm đúng vị trí `<project_root>/components/ota_manager/` (ngang hàng `main/`).

---

## Bảng tra nhanh

| Hàm | Khai báo trong | Gọi ở đâu (theo đúng `esp32c5_rak3172`) | Tham số | Trả về |
|---|---|---|---|---|
| `ota_manager_start` | `ota_manager.h` | `mqtt_event_handler`, `case MQTT_EVENT_CONNECTED` | `esp_mqtt_client_handle_t client` | `void` |
| `ota_manager_handle_mqtt_data` | `ota_manager.h` | đăng ký trong `s_mqtt_routes[]` (hoặc gọi trực tiếp trong `MQTT_EVENT_DATA` nếu chưa có dispatch table) | `const esp_mqtt_event_handle_t event` | `void` |
| `ota_manager_is_busy` | `ota_manager.h` | bất kỳ đâu cần biết "đang OTA hay không" trước khi làm việc gì có thể bị gián đoạn | *(không có)* | `bool` |
| `ota_rollback_confirm_if_pending` | `ota_rollback.h` | tại checkpoint bạn chọn (thường cùng chỗ với `ota_manager_start`) | *(không có)* | `void` |
| `ota_rollback_start_confirm_watchdog` | `ota_rollback.h` | đầu hàm `start()` của transport WiFi/MQTT, TRƯỚC khi kết nối WiFi | `uint32_t timeout_ms` | `void` |

`ota_manager_notify_task_done()` và toàn bộ `ota_task.c`/`.h` là nội bộ — `ota_manager.c` tự gọi, code EMF không cần gọi trực tiếp.

---

## ⚠️ TUYỆT ĐỐI KHÔNG COPY

- **`components/wifi_manager/`** của `esp32c5_rak3172` — EMF đã có driver WiFi chạy sẵn. Copy vào sẽ khiến `esp_wifi_init()`/`esp_netif_create_default_wifi_sta()` bị gọi lần thứ 2, xung đột với code hiện tại.
- **`main/main.c`/`components/transport/` nguyên file** của `esp32c5_rak3172` — chỉ tham khảo cách wire 4 điểm nối, không copy. Code EMF phải giữ nguyên toàn bộ nghiệp vụ hiện tại, chỉ thêm đúng 4 lời gọi hàm + 2 dòng include (+ 1 dòng dispatch table nếu EMF có cơ chế đó).

---

## Điều kiện ngầm định cần tự verify trước khi port

1. **EMF đã gọi `esp_event_loop_create_default()` từ trước** — `ota_task.c` đăng ký lắng nghe `ESP_HTTPS_OTA_EVENT` trên default event loop. Vì EMF đã có WiFi/MQTT chạy, gần như chắc chắn loop này đã tồn tại — chỉ cần xác nhận.
2. **EMF dùng đúng thư viện `esp-mqtt` chuẩn ESP-IDF** (`esp_mqtt_client_handle_t`/`esp_mqtt_event_handle_t`) — nếu dùng thư viện MQTT khác, phải viết lại phần ký hiệu tham số trong `ota_manager.h`/`ota_manager.c`.
3. **✅ ĐÃ XÁC NHẬN, KHÔNG PHẢI GIẢ ĐỊNH:** partition table hiện tại của EMF **CÓ** partition `factory` (xem `FLASH_ANALYSIS.md` mục 2 — đo trực tiếp trên board thật). Bảng partition mới theo `RETROFIT_OTA_ESP32C5.md`/`partitions.csv` **KHÔNG còn `factory`** — **bắt buộc** rà lại code nghiệp vụ EMF xem có chỗ nào kiểm tra tên/địa chỉ partition `factory` hoặc logic factory-reset gắn với partition đó không, trước khi áp dụng partition table mới.

## Sau khi port xong — vẫn cần vá trước khi dùng thật

- `is_version_newer()` trong `ota_manager.c` chỉ `strcmp` khác nhau, chưa chặn được downgrade.
- `OTA_COMMAND_TOPIC` chưa có xác thực nếu broker là broker công cộng/ẩn danh.

Xem chi tiết đầy đủ 2 điểm này tại `/home/vietnq/esp/ota/docs/KNOWN_ISSUES.md` mục 1 (`ota_manager.c` giống hệt giữa 2 project, lỗi này tồn tại ở cả 2 nơi).

## Diff xác nhận giữa nguồn `esp32c5_rak3172` và bản gốc `esp/ota`

Đã `diff -rq` toàn bộ `components/ota_manager/` giữa 2 project để xác nhận chính xác cái gì khác nhau (không suy đoán):
- `ota_task.c`, `ota_rollback.c`, `Kconfig`, `CMakeLists.txt` — **giống hệt 100%**.
- `ota_manager.h`/`ota_manager.c` — chỉ khác: tên topic (namespace riêng) + thêm hàm `ota_manager_is_busy()`.
- `ota_rollback.h` — khác 1 chữ trong comment (không ảnh hưởng logic).
