# Kế hoạch: Thêm tính năng OTA cho project `ota` (ESP32)

## Context

Project `/home/vietnq/esp/ota` hiện đã có sẵn phần WiFi (STA) + MQTT client (kết nối `broker.hivemq.com:1883`, subscribe vài topic test, log bản tin nhận được) trong `main/main.c`. Yêu cầu tiếp theo là thêm tính năng OTA thật sự, kích hoạt từ xa qua MQTT, với đầy đủ các lớp bảo vệ (safe update, rollback, ký số firmware) và khả năng giám sát tiến trình qua MQTT — dựa trên 2 tài liệu đã đọc (`/home/vietnq/esp/doc/ota/*.md`) và đã đối chiếu với source thật của ESP-IDF (đã xác minh đúng cho bản **v5.5.5** — bản đang cài trong máy, không phải v6.1 dùng trong tài liệu, nhưng API giống nhau ở mọi điểm đã kiểm tra: `esp_https_ota_config_t`, event enum, rollback API, Kconfig `SECURE_SIGNED_APPS_NO_SECURE_BOOT`/`SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`, `PARTITION_TABLE_TWO_OTA`, `espsecure.py generate_signing_key`).

Yêu cầu cụ thể (đã bỏ Anti-rollback theo chỉnh sửa của người dùng):
1. Trigger OTA khi nhận bản tin MQTT (JSON `{version, url}`)
2. Safe update mode (ghi vào slot không chạy, chỉ chuyển boot sau khi verify xong)
3. App Rollback (tự phục hồi nếu firmware mới lỗi)
4. Secure OTA Updates Without Secure Boot (ký số firmware, verify khi OTA)
5. Tuning OTA Performance (bulk erase, buffer lớn, buffer nội bộ RAM)
6. OTA qua HTTPS, server dựng trên máy local
7. Bảo mật đường truyền (đã có sẵn nhờ HTTPS/TLS ở mục 6, không cần thêm gì)
8. Gửi trạng thái từng bước OTA lên MQTT (hivemq) để giám sát
9. Tổ chức code OTA vào 1 folder riêng
10. Báo rõ bước nào cần người dùng tự cấu hình/thao tác thủ công

**3 quyết định thiết kế đã chốt với người dùng:**
- HTTPS server local sẽ chạy **trực tiếp trên Windows** (không chạy trong WSL, vì WSL2 NAT sẽ khiến ESP32 không kết nối được nếu không port-forward).
- HTTPS server viết bằng **Node.js** (module `https` built-in, không cần thư viện ngoài) thay vì Python/OpenSSL.
- Cơ chế xác nhận rollback ("app mới ổn") = **tự động khi WiFi + MQTT kết nối thành công** (không cần GPIO vật lý), giống cách `advanced_https_ota` chính thức làm.

**Đã xác nhận với người dùng:** board có **4MB flash** thật (đã cập nhật `Flash size` trong menuconfig) và đã tự chọn sẵn `Partition Table` → `Factory app, two OTA definitions` (`CONFIG_PARTITION_TABLE_TWO_OTA`). Bước 0 và Bước 1 trong danh sách "cần người dùng tự làm" bên dưới **đã hoàn thành**, không cần tạo `partitions.csv` tùy chỉnh — dùng thẳng partition table có sẵn của ESP-IDF (`factory` + `ota_0` + `ota_1`, mỗi cái 1MB).

---

## Kiến trúc tổng thể

```
┌──────────────┐   MQTT "esp32/vietnq/ota/cmd"   ┌─────────────────────────┐
│ Bạn (mosquitto│ ───────────────────────▶│         ESP32           │
│ _pub / dashboard) │                      │ main.c + wifi_manager   │
└──────────────┘                          │        │                │
                                            │        ▼                │
                                            │  components/ota_manager │
                                            │  (parse lệnh, guard,    │
                                            │   trigger OTA task)     │
                                            └────────┬────────────────┘
                                                     │ HTTPS (TLS, self-signed cert)
                                                     ▼
                                         ┌────────────────────────┐
                                         │ Node.js HTTPS server   │
                                         │ (Windows) serve ota.bin│
                                         └────────────────────────┘
                     MQTT "esp32/vietnq/ota/status" (từng bước OTA)
ESP32 ──────────────────────────────────────────────────────▶ broker.hivemq.com ──▶ bạn giám sát (mosquitto_sub)
```

---

## Cấu trúc file (tổ chức OTA thành 1 component riêng — chuẩn ESP-IDF)

```
esp/ota/
├── version.txt                           (MỚI — chứa version app, vd "1.0.0")
├── components/
│   ├── ota_manager/                      (MỚI — toàn bộ logic OTA phía ESP32 nằm ở đây)
│   │   ├── CMakeLists.txt                (EMBED_TXTFILES server_certs/ca_cert.pem)
│   │   ├── Kconfig                       (option OTA_MANAGER_ALLOW_HTTP, xem "Chuyển đổi giữa HTTP và HTTPS")
│   │   ├── include/ota_manager.h         (API công khai + macro topic)
│   │   ├── ota_manager.c                 (nhận lệnh MQTT, parse JSON, guard trùng lặp/version/scheme)
│   │   ├── ota_rollback.h / .c           (xác nhận rollback khi có checkpoint)
│   │   ├── ota_task.h / .c               (luồng esp_https_ota: begin→perform→finish, tuning, publish status từng bước)
│   │   └── server_certs/
│   │       └── ca_cert.pem               (PLACEHOLDER — người dùng thay bằng cert thật, xem Bước 6)
│   └── wifi_manager/                     (MỚI — toàn bộ logic kết nối WiFi STA, độc lập với MQTT/OTA)
│       ├── CMakeLists.txt
│       ├── include/WiFi.h                (API công khai: wifi_manager_start())
│       └── WiFi.c                        (connect, retry, log lý do mất kết nối)
├── ota_https_server/                     (MỚI — server Node.js, chạy trên Windows, KHÔNG build cùng firmware)
│   ├── server-https.js                   (HTTPS static file server bằng module `https` built-in — bản chính, dùng thật)
│   ├── server-http.js                    (server HTTP thuần, KHÔNG TLS — chỉ dev/test khi bật CONFIG_OTA_MANAGER_ALLOW_HTTP)
│   ├── certs/                            (copy ca_cert.pem + ca_key.pem vào đây, xem Bước 6; kèm README.txt nhắc lại)
│   └── firmware/                         (copy build/ota.bin vào đây trước khi test OTA; kèm README.txt nhắc lại)
└── main/
    ├── CMakeLists.txt                    (thêm REQUIRES ota_manager, wifi_manager)
    └── main.c                            (gọi wifi_manager_start(), MQTT; gọi ota_manager_start() khi MQTT connect, ota_manager_handle_mqtt_data() khi có bản tin, ota_rollback_confirm_if_pending() ở checkpoint)
```

### API công khai (`ota_manager.h`)

```c
#define OTA_COMMAND_TOPIC "esp32/vietnq/ota/cmd"      // Server -> Device: {"version":"1.0.1","url":"https://<ip>:8070/ota.bin"}
#define OTA_STATUS_TOPIC  "esp32/vietnq/ota/status"   // Device -> Server: {"status":"WRITING_FLASH","progress":123456}

void ota_manager_start(esp_mqtt_client_handle_t client);                 // subscribe OTA_COMMAND_TOPIC, lưu client để publish status
void ota_manager_handle_mqtt_data(const esp_mqtt_event_handle_t event);  // gọi trong case MQTT_EVENT_DATA của main.c
```

`ota_rollback.h`:
```c
void ota_rollback_confirm_if_pending(void); // gọi 1 lần tại checkpoint (MQTT_EVENT_CONNECTED lần đầu)
```

### Điểm nối vào `main/main.c` (thay đổi tối thiểu, tái dùng mqtt_event_handler đã có)

- `case MQTT_EVENT_CONNECTED`: sau đoạn subscribe `s_topic_list` hiện có → gọi `ota_manager_start(client)` + `ota_rollback_confirm_if_pending()` (chỉ chạy logic 1 lần nhờ cờ static bên trong `ota_rollback.c`).
- `case MQTT_EVENT_DATA`: gọi thêm `ota_manager_handle_mqtt_data(event)` (hàm tự bỏ qua nếu topic không phải `esp32/vietnq/ota/cmd`).

### Các trạng thái publish lên MQTT trong lúc OTA

Chỉ publish lên **1 topic duy nhất**: `esp32/vietnq/ota/status` (`OTA_STATUS_TOPIC`). Topic `esp32/vietnq/ota/cmd` chỉ để subscribe nhận lệnh, không publish. Payload luôn dạng `{"status":"<TÊN>","progress":<số>}`, QoS 1.

Thứ tự thực tế (theo code trong `ota_task.c`), khi 1 lượt OTA thành công trọn vẹn:

| # | Trigger | Payload | Khi nào |
|---|---|---|---|
| 1 | `ESP_HTTPS_OTA_START` | `{"status":"OTA_START","progress":0}` | Vừa gọi `esp_https_ota_begin()` |
| 2 | `ESP_HTTPS_OTA_CONNECTED` | `{"status":"CONNECTED","progress":0}` | Kết nối TCP/TLS tới server local thành công |
| 3 | `ESP_HTTPS_OTA_GET_IMG_DESC` | `{"status":"READING_IMG_DESC","progress":0}` | Đang đọc header mô tả app (version...) của file mới |
| 4 | `ESP_HTTPS_OTA_VERIFY_CHIP_ID` | `{"status":"VERIFYING_CHIP_ID","progress":0}` | Kiểm tra firmware build đúng cho chip ESP32 |
| 5 | `ESP_HTTPS_OTA_VERIFY_CHIP_REVISION` | `{"status":"VERIFYING_CHIP_REVISION","progress":0}` | Kiểm tra tương thích revision chip |
| 6 | `ESP_HTTPS_OTA_WRITE_FLASH` | `{"status":"WRITING_FLASH","progress":<số byte đã ghi>}` | **Bắn LẶP LẠI liên tục** trong lúc tải+ghi flash (progress tăng dần) |
| 7 | `ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION` | `{"status":"BOOT_PARTITION_UPDATED","progress":100}` | Vừa xác nhận chữ ký OK, chuẩn bị chuyển boot partition |
| 8 | `ESP_HTTPS_OTA_FINISH` | `{"status":"OTA_FINISH","progress":100}` | `esp_https_ota_finish()` hoàn tất |
| 9 | (code tự publish, không qua event) | `{"status":"REBOOTING","progress":100}` | Ngay trước khi gọi `esp_restart()` |

Các tình huống lỗi cũng bắn trạng thái riêng:
- **Kết nối tới HTTPS server thất bại** (`esp_https_ota_begin()` trả lỗi — sai IP/port, TLS handshake fail do cert không khớp CN, server chưa chạy...): `{"status":"HTTPS_CONNECT_FAILED","progress":-1}`. Lưu ý: `esp_https_ota_begin()` chỉ dispatch event `ESP_HTTPS_OTA_CONNECTED` khi connect **thành công** — nếu connect thất bại thì không có event nào bắn ra cả, nên phải tự publish thêm dòng này ngay tại nhánh `if (err != ESP_OK)` sau lệnh gọi, nếu không sẽ chỉ thấy lỗi qua serial log chứ không thấy qua MQTT.
- Chữ ký sai (`esp_https_ota_finish()` trả `ESP_ERR_OTA_VALIDATE_FAILED`): `{"status":"SIGNATURE_INVALID","progress":-1}`
- Lỗi/hủy khác giữa chừng (event `ESP_HTTPS_OTA_ABORT`): `{"status":"OTA_ABORT","progress":-1}`

**Tóm lại 2 chiều kết nối HTTPS đã được báo đủ lên MQTT:**
- Kết nối **thành công** → dòng #2 ở bảng trên (`CONNECTED`)
- Kết nối **thất bại** → dòng `HTTPS_CONNECT_FAILED` vừa thêm

**Khoảng trống hiện tại (chưa xử lý):** nếu lệnh OTA bị bỏ qua ngay từ đầu (đang OTA dở, version không mới hơn, hoặc JSON sai định dạng), `ota_manager.c` hiện chỉ `ESP_LOGW`/`ESP_LOGE` ra serial console, **chưa publish** gì lên `esp32/vietnq/ota/status` — muốn giám sát được cả các trường hợp "lệnh bị từ chối" qua MQTT thì cần bổ sung thêm publish cho các nhánh đó (vd `IGNORED_ALREADY_RUNNING`, `IGNORED_NOT_NEWER`).

---

## Ánh xạ yêu cầu → implementation

| # | Yêu cầu | Cách làm | File |
|---|---|---|---|
| 1 | Trigger qua MQTT | Parse JSON `{version,url}` bằng `cJSON` (component `json` có sẵn), guard bằng cờ `ota_in_progress` (volatile bool) + so sánh `version` với `esp_ota_get_partition_description()` của partition đang chạy | `ota_manager.c` |
| 2 | Safe update mode | Dùng partition table có ≥2 OTA slot (đã chọn `Two OTA definitions`, xem Bước 0-1 ✅) + `esp_https_ota` (tự ghi vào slot không chạy, chỉ chuyển boot sau `esp_https_ota_finish()` thành công) | `ota_task.c` |
| 3 | App Rollback | Bật `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`; code check `ESP_OTA_IMG_PENDING_VERIFY` → `esp_ota_mark_app_valid_cancel_rollback()` tại checkpoint. Kèm 1 đồng hồ cảnh báo độc lập (`ota_rollback_start_confirm_watchdog()`) chủ động rollback nếu quá hạn chưa confirm — xem mục "Đồng hồ cảnh báo xác nhận rollback" bên dưới | `ota_rollback.c`, menuconfig (Bước 2) |
| 4 | Secure OTA No Secure Boot | Bật `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` + `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`, trỏ `CONFIG_SECURE_BOOT_SIGNING_KEY` tới file khóa ký (tạo bằng `espsecure.py`) | menuconfig (Bước 3, 4) — tự động ký mỗi lần `idf.py build`, verify tự động trong `esp_https_ota_finish()` |
| 5 | Tuning Performance | `esp_https_ota_config_t{bulk_flash_erase=true, buffer_caps=MALLOC_CAP_INTERNAL\|MALLOC_CAP_8BIT}` + `esp_http_client_config_t.buffer_size` lớn (4096). ⚠️ Thiếu `MALLOC_CAP_8BIT` từng gây crash thật trên board — xem mục "Sự cố đã gặp" bên dưới | `ota_task.c`, menuconfig (Bước 7) |
| 6 | HTTPS local server | Cert self-signed CN=IP LAN Windows, nhúng `ca_cert.pem` vào firmware qua `EMBED_TXTFILES`; server Node.js (`https.createServer` + đọc `ca_cert.pem`/`ca_key.pem`) serve file `.bin` tĩnh | `ota_manager/CMakeLists.txt`, `server_certs/`, `ota_https_server/server-https.js`, Bước 5–6 |
| 7 | Bảo mật đường truyền | Đã có sẵn nhờ TLS ở mục 6. Scheme (http/https) đọc thẳng từ `url` trong lệnh MQTT — không hardcode; mặc định (`CONFIG_OTA_MANAGER_ALLOW_HTTP=n`) chặn mọi `url` không phải `https://` ngay từ `ota_manager.c`, tránh bị hạ cấp xuống HTTP ngoài ý muốn. Xem "Chuyển đổi giữa HTTP và HTTPS" bên dưới | `ota_manager.c` (guard), `ota_task.c` (`cert_pem` theo scheme), `Kconfig` |
| 8 | Giám sát qua MQTT (hivemq) | Đăng ký `esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ...)`, mỗi event publish JSON `{"status":...,"progress":...}` lên `OTA_STATUS_TOPIC` qua client MQTT đã kết nối hivemq sẵn có | `ota_task.c` |
| 9 | Tổ chức folder riêng | `components/ota_manager/` (ESP-IDF component chuẩn) | toàn bộ mục trên |

---

## Các bước CẦN NGƯỜI DÙNG tự làm (mình sẽ dừng lại và hướng dẫn cụ thể khi tới từng bước lúc code)

> Thứ tự thực hiện: viết code trước (không phụ thuộc các giá trị này) → rồi mới cần bạn làm các bước dưới để build/chạy được thật.

**Bước 0 — ✅ ĐÃ XONG: Xác nhận flash size thật của board**

Vì sao cần: partition table 2 OTA slot (`factory` + `ota_0` + `ota_1`, mỗi cái 1MB) cần tối thiểu 4MB flash — nếu board thật nhỏ hơn mà vẫn để cấu hình flash 2MB thì lúc build sẽ báo lỗi hoặc lúc flash sẽ ghi đè sai vùng nhớ.

Cách làm:
1. Đóng mọi cửa sổ `idf.py monitor` đang mở tới board (monitor giữ độc quyền cổng serial, esptool không đọc được nếu cổng đang bị chiếm).
2. Chạy lệnh đọc thông tin chip qua serial (không ghi gì lên board, an toàn):
   ```bash
   esptool.py --port /dev/ttyUSB0 flash_id
   ```
3. Đọc dòng `Detected flash size: ...` trong kết quả trả về — đây là dung lượng flash **vật lý thật**, độc lập với giá trị đang cấu hình trong `sdkconfig`.
4. Vào `idf.py menuconfig` → `Serial flasher config` → `Flash size` → chọn đúng giá trị vừa đo được (ở đây là `4 MB`).

Kết quả: đã xác nhận board có 4MB flash thật, đã cập nhật `CONFIG_ESPTOOLPY_FLASHSIZE=4MB` trong `sdkconfig`.

**Bước 1 — ✅ ĐÃ XONG: Chọn Partition Table (Safe Update Mode)**

Vì sao cần: mặc định project chỉ có 1 app partition (`CONFIG_PARTITION_TABLE_SINGLE_APP`) — không có chỗ để ghi firmware mới song song với firmware đang chạy, nên không thể OTA an toàn (safe update). Cần đổi sang partition table có ≥2 OTA slot.

Cách làm:
1. `idf.py menuconfig`
2. Vào mục `Partition Table`
3. Chọn `Factory app, two OTA definitions` (tương ứng `CONFIG_PARTITION_TABLE_TWO_OTA=y`) — đây là partition table dựng sẵn của ESP-IDF, không cần tự viết file `.csv`.
4. Lưu lại (phím `Q` → `Y` nếu dùng menuconfig dạng terminal UI, hoặc bấm Save nếu dùng menuconfig của VS Code extension).

Kết quả: partition table hiện có 3 app partition — `factory` (firmware gốc, không rollback được), `ota_0`, `ota_1` (2 slot OTA luân phiên), mỗi cái 1MB — cùng 1 partition `otadata` (8KB, lưu trạng thái/slot đang chạy). Đây chính là điều kiện tiên quyết để `esp_https_ota` hoạt động theo cơ chế Safe Update Mode (ghi vào slot không chạy, chỉ chuyển boot sau khi verify xong).

**Bước 2 — Bật App Rollback**
`idf.py menuconfig` → `Bootloader config` → tick `Enable app rollback support`.

**Bước 3 — ✅ ĐÃ XONG: Tạo khóa ký firmware**
Chạy (trong WSL, đã có `espsecure.py` từ ESP-IDF cài sẵn):
```bash
cd /home/vietnq/esp/ota
espsecure.py generate_signing_key --version 1 --scheme ecdsa256 secure_boot_signing_key.pem
```
Đặt tên `secure_boot_signing_key.pem` (thay vì tên tùy ý khác) vì đây đúng bằng tên mặc định mà `idf.py menuconfig` gợi ý ở Bước 4 — khỏi phải sửa lại đường dẫn trong đó.

⚠️ File `secure_boot_signing_key.pem` là khóa riêng tư — không commit vào git, không chia sẻ.

**Bước 4 — ✅ ĐÃ XONG: Bật Secure OTA Without Secure Boot**
`idf.py menuconfig` → `Security features`:
- Tick `Require signed app images` (`SECURE_SIGNED_APPS_NO_SECURE_BOOT`)
- Xác nhận `App Signing Scheme` = `ECDSA` (tự chọn mặc định cho ESP32)
- Tick `Verify app signature on update` (thường tự bật theo mục trên)
- Tick `Sign binaries during build` (để `idf.py build` tự ký, không cần thao tác ký thủ công riêng)
- Giữ nguyên giá trị mặc định `secure_boot_signing_key.pem` ở ô `Secure boot private signing key` — đã khớp với tên file tạo ở Bước 3
- **Không** tick `Enable hardware Secure Boot in bootloader` và **không** tick `Enable flash encryption on boot` — đúng tinh thần "Without Secure Boot"

#### Cơ chế ký số hoạt động thế nào (giải thích chi tiết)

**Đây KHÔNG phải mã hóa (encryption)** — firmware `.bin` vẫn là plaintext bình thường, chỉ được gắn thêm 1 khối chữ ký (signature block) ở cuối. Mã hóa thật sự là tính năng khác (`Enable flash encryption on boot`), không dùng trong plan này.

**Public key được nhúng vào app từ đâu?** Không phải bước thủ công riêng — nó **tự động xảy ra trong mỗi lần `idf.py build`**, là hệ quả của việc bật `Sign binaries during build` ở trên. Cơ chế thật (đã đọc trong `components/bootloader_support/CMakeLists.txt` của ESP-IDF):

```
components/bootloader_support/CMakeLists.txt (chạy mỗi lần build)
│
├─ B1: Trích public key TỪ private key tạo ở Bước 3
│      espsecure.py extract_public_key
│        --keyfile secure_boot_signing_key.pem      ← khóa PRIVATE (Bước 3, giữ bí mật)
│        signature_verification_key.bin              ← khóa PUBLIC, sinh mới mỗi build
│
└─ B2: Nhúng file public key đó thẳng vào app đang build
       target_add_binary_data(bootloader_support signature_verification_key.bin ...)
       → tạo symbol _binary_signature_verification_key_bin_start/_end
       → linker gộp thẳng vào file .elf/.bin của app
```

Cơ chế nhúng (`target_add_binary_data`) về bản chất **giống hệt** cách file `ca_cert.pem` được nhúng vào firmware qua `EMBED_TXTFILES` trong `ota_manager/CMakeLists.txt` — chỉ khác là công việc này do component `bootloader_support` tự làm, không phải do mình viết.

**Chuỗi phụ thuộc đầy đủ:**
```
secure_boot_signing_key.pem (Bước 3 — khóa PRIVATE, không commit git, chỉ dùng lúc build trên máy dev)
        │
        ▼  espsecure.py extract_public_key (tự động, mỗi lần build)
signature_verification_key.bin (khóa PUBLIC — không bí mật)
        │
        ▼  target_add_binary_data (tự động, mỗi lần build)
Nhúng vào file .bin của app đang chạy trên board
        │
        ▼  dùng khi có OTA mới tới
esp_https_ota_finish() → esp_ota_end() → ota_verify_partition()
        │
   Verify chữ ký của firmware MỚI tải về, bằng public key đã nhúng sẵn
        │
   ├── OK    → esp_ota_set_boot_partition() → lần boot tới chạy firmware mới → esp_restart()
   └── SAI   → KHÔNG đổi boot partition, firmware mới bị vứt, vẫn chạy bản cũ,
                trả lỗi ESP_ERR_OTA_VALIDATE_FAILED (code đã bắt lỗi này trong ota_task.c,
                publish trạng thái "SIGNATURE_INVALID" lên MQTT)
```

**Điểm quan trọng cần nhớ:**
- Private key (`secure_boot_signing_key.pem`) **không bao giờ nằm trong firmware** — chỉ dùng lúc build trên máy dev để (a) ký file `.bin` và (b) trích ra public key. Mất file này = phải tạo khóa mới và các firmware cũ đã ký sẽ không còn tương thích logic ký với bản mới (vẫn boot bình thường, chỉ là không dùng để ký tiếp được).
- Việc verify chữ ký chỉ chạy **đúng 1 lần cho mỗi lần OTA** (ngay trong `esp_https_ota_finish()`), **không** chạy lại ở mỗi lần bật nguồn — vì đã để `Bootloader verifies app signatures` tắt (không bật hardware Secure Boot). Cơ chế này bảo vệ kênh OTA qua mạng, không bảo vệ chống truy cập vật lý (cắm dây USB flash tay vẫn không bị chặn).

**Bước 5 — ✅ ĐÃ XONG: Xác định IP LAN của máy Windows**
Trên Windows, mở PowerShell/CMD chạy `ipconfig`, lấy IPv4 của adapter đang cùng mạng WiFi với ESP32. Đã xác định: `192.168.2.7`.

**Bước 6 — ✅ ĐÃ XONG: Tạo self-signed certificate (chạy trên Windows)**

Windows (CMD/PowerShell) mặc định **không có sẵn** lệnh `openssl` trong PATH, và máy này cũng không có `winget` để cài nhanh. Cách đã dùng: tận dụng bản `openssl.exe` đi kèm sẵn theo **Git for Windows** (hầu hết máy dev đều có Git cài sẵn).

1. Xác định Git cài ở đâu:
   ```cmd
   where git
   ```
   Kết quả: `C:\Program Files\Git\cmd\git.exe` → suy ra thư mục gốc Git là `C:\Program Files\Git`, và openssl đi kèm nằm ở `C:\Program Files\Git\usr\bin\openssl.exe`.

2. Chạy lệnh tạo certificate, gọi thẳng bằng đường dẫn đầy đủ tới openssl (không cần thêm vào PATH):
   ```cmd
   "C:\Program Files\Git\usr\bin\openssl.exe" req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365 -nodes -subj "/CN=192.168.2.7"
   ```
   Lệnh sẽ in ra một chuỗi dấu `.`/`+` khi sinh khóa RSA (bình thường, không phải lỗi) rồi kết thúc, tạo ra 2 file `ca_key.pem` và `ca_cert.pem` trong thư mục hiện tại.

   > Nếu muốn gõ ngắn gọn `openssl ...` cho các lần sau: thêm `C:\Program Files\Git\usr\bin` vào biến môi trường `PATH` (System Properties → Environment Variables → Path → New), rồi mở lại cửa sổ CMD mới.

3. Sau khi có 2 file, phân phối như sau:
   - Copy `ca_cert.pem` (KHÔNG copy `ca_key.pem`) vào `components/ota_manager/server_certs/ca_cert.pem` trong WSL — để nhúng vào firmware. Có thể gõ thẳng đường dẫn UNC của WSL vào thanh địa chỉ File Explorer: `\\wsl$\Ubuntu\home\vietnq\esp\ota\components\ota_manager\server_certs\`
   - Copy CẢ HAI file `ca_cert.pem` và `ca_key.pem` vào `ota_https_server/certs/` (trên Windows) — để server Node.js dùng khi chạy HTTPS.

#### Cơ chế ESP32 verify HTTPS server hoạt động thế nào

```
components/ota_manager/CMakeLists.txt:
    EMBED_TXTFILES "server_certs/ca_cert.pem"
        │
        ▼  (lúc build, cùng cơ chế nhúng public key ở mục "Cơ chế ký số" phía trên)
    Linker tạo symbol _binary_ca_cert_pem_start / _binary_ca_cert_pem_end
        │
        ▼
ota_task.c:
    extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
        │
        ▼  dùng làm root cert để verify server
    esp_http_client_config_t.cert_pem = (char *)server_cert_pem_start;
        │
        ▼  esp_https_ota_begin() mở kết nối TLS tới https://192.168.2.7:8070
Lúc handshake TLS: server (Node.js) gửi cert của nó (ca_cert.pem, CN=192.168.2.7)
        │
   ESP32 so sánh cert server gửi ↔ cert đã nhúng sẵn (server_cert_pem_start)
        │
   ├── KHỚP     → handshake OK → tải firmware bình thường
   └── KHÔNG KHỚP → lỗi TLS, esp_https_ota_begin() thất bại, không tải được gì
```

Đã xác nhận bằng `nm` trên file `.elf` sau khi build — cả 2 symbol đều thật sự có trong firmware:
```
_binary_ca_cert_pem_start / _end                      ← cert HTTPS server (Bước 6, bảo vệ ĐƯỜNG TRUYỀN)
_binary_signature_verification_key_bin_start / _end   ← public key ký firmware (Bước 3-4, bảo vệ NỘI DUNG file)
```

**2 thứ này khác mục đích hoàn toàn, đừng nhầm lẫn:**

| | `ca_cert.pem` | `signature_verification_key.bin` |
|---|---|---|
| Bảo vệ cái gì | **Kênh truyền** (transport) | **Nội dung file** (firmware mới) |
| Dùng khi nào | Lúc TLS handshake với HTTPS server | Lúc `esp_https_ota_finish()`, sau khi tải xong toàn bộ file |
| Trả lời câu hỏi | "Mình có đang nói chuyện với đúng server không, hay bị man-in-the-middle?" | "File `.bin` vừa tải về có đúng do mình build/ký ra không, hay bị giả mạo?" |
| Nguồn gốc | Tự tạo bằng `openssl` (Bước 6) | Tự động trích từ `secure_boot_signing_key.pem` lúc build (Bước 3-4) |
| Cơ chế nhúng | `EMBED_TXTFILES` (code tự viết) | `target_add_binary_data` (ESP-IDF tự làm) |

Cả 2 bổ trợ nhau — mất 1 trong 2 thì lớp còn lại vẫn bảo vệ được 1 phần.

**2 điều kiện bắt buộc để verify TLS thành công:**
1. CN trong cert phải đúng bằng host trong URL OTA (`192.168.2.7`) — đổi IP máy Windows thì phải tạo lại cert (code hiện không bật `skip_cert_common_name_check`).
2. Phải `idf.py build` lại mỗi khi thay `ca_cert.pem` thật — file cũ đã nhúng vào `.bin` cũ sẽ không tự cập nhật.

#### ⚠️ Đây là chứng chỉ TỰ KÝ (self-signed) — chỉ phù hợp môi trường dev/test

Cert tạo ở Bước 6 (`openssl req -x509 ...`) là **self-signed**: tự ký chính nó, không có CA công cộng nào đứng ra xác nhận "đây đúng là 192.168.2.7 của bạn". Nó chỉ hoạt động được vì mình **nhúng cứng đúng file này** vào firmware làm "root tin cậy" — ESP32 không hỏi ý kiến ai khác, cứ thấy khớp bit-by-bit là tin. Đây là cách làm chấp nhận được cho dev/test nội bộ, cùng LAN, nhưng có 2 hạn chế cần biết:
- Đổi IP server hoặc cert hết hạn (365 ngày) → phải tạo cert mới **và** build lại firmware mới nhúng được — không thể "gia hạn" cert mà không đụng tới firmware.
- Không có cơ chế thu hồi (revocation) nếu private key `ca_key.pem` bị lộ — phải tạo cặp khóa mới + rebuild toàn bộ firmware đang chạy ngoài field.

**Nếu triển khai production, cần xin/chuẩn bị gì:**

| Kịch bản production | File cần xin | Để vào đâu |
|---|---|---|
| Server OTA có domain public thật (vd `ota.congty.com`) | Certificate từ CA công cộng (Let's Encrypt, DigiCert...) — thường là file `fullchain.pem` (cert + chain) và `privkey.pem` | **Không cần nhúng gì vào firmware nữa.** Đổi `esp_http_client_config_t.cert_pem` thành `.crt_bundle_attach = esp_crt_bundle_attach` (bật `Component config → mbedTLS → Certificate Bundle` trong menuconfig) — ESP-IDF đã có sẵn bundle chứa hầu hết root CA công cộng, tự verify được server có cert từ CA thật mà không cần biết trước. |
| Server OTA nội bộ, dùng PKI riêng của công ty (không có domain public) | **Root CA certificate** của công ty (file `.pem`/`.crt` gốc do team hạ tầng/bảo mật cấp — KHÔNG phải cert của riêng server OTA) | `components/ota_manager/server_certs/ca_cert.pem` — giống hệt cơ chế đang làm, chỉ khác là nhúng cert của **CA gốc công ty** thay vì self-signed. Ưu điểm: server đổi cert lẻ (gia hạn, đổi IP) không cần rebuild firmware, miễn cert mới vẫn do CA gốc đó ký. |
| Khóa ký firmware (`secure_boot_signing_key.pem`) | — | Không "xin" ai, nhưng cần **quản lý như bí mật cấp cao**: lưu trong HSM/key vault (không để plain file trên máy dev), giới hạn người có quyền build release, cân nhắc bật thêm hardware Secure Boot thật nếu cần chống cả truy cập vật lý. |

#### Chuyển đổi giữa HTTP và HTTPS (cấu hình, không cần sửa code)

Mặc định firmware chỉ chấp nhận `url` dạng `https://...`. Cách chọn transport được thiết kế để **đổi qua lệnh MQTT, không cần sửa code/build lại**, nhưng có 1 khóa an toàn ở mức Kconfig để không bị hạ cấp (downgrade) ngoài ý muốn:

- **`ota_task.c`**: `esp_http_client_config_t.cert_pem` được gán **có điều kiện** — chỉ gán cert nhúng sẵn khi `url` bắt đầu bằng `https://`, còn lại để `NULL`. Tức là bản thân transport (http hay https) hoàn toàn do `url` trong lệnh MQTT quyết định.
- **`ota_manager.c`**: ngay sau khi parse JSON, nếu Kconfig `OTA_MANAGER_ALLOW_HTTP` đang **tắt** (mặc định), mọi `url` không bắt đầu bằng `https://` bị từ chối ngay tại đây — không giao xuống `ota_task` nữa.
- Việc **xác thực chữ ký firmware (Secure OTA Without Secure Boot)** hoàn toàn độc lập với lớp transport này — dù chạy HTTP hay HTTPS, `esp_https_ota_finish()` vẫn luôn kiểm tra chữ ký trước khi chuyển boot. HTTP chỉ mất tính bảo mật/toàn vẹn *trên đường truyền* (có thể bị nghe lén hoặc sửa gói tin giữa đường trước khi tới bước verify), không mất khả năng phát hiện firmware giả mạo.

**Cách bật HTTP (chỉ nên dùng khi dev/test cục bộ, không dùng khi có thiết bị ngoài field):**

1. Mở menuconfig — 1 trong 2 cách:
   - Terminal: `idf.py menuconfig` (chưa có lệnh `idf.py`? chạy `. ~/esp-idf/export.sh` trước).
   - VS Code (đang mở sẵn IDE): `Ctrl+Shift+P` → gõ **"ESP-IDF: SDK Configuration Editor (menuconfig)"** → dùng ô search gõ thẳng tên option cho nhanh.
2. Bật **option thứ nhất** (do `ota_manager` tự định nghĩa, xem `components/ota_manager/Kconfig`):
   ```
   Component config → OTA Manager → Allow OTA over plain HTTP (insecure transport)   [bật]
   ```
3. Bật **option thứ hai** — của chính thư viện `esp_https_ota` (ESP-IDF), KHÔNG phải do project này định nghĩa:
   ```
   Component config → ESP HTTPS OTA → Allow HTTP for OTA   [bật]
   ```
   ⚠️ **Thiếu bước này thì vẫn lỗi dù đã làm đúng bước 2** — đã gặp thật khi test: log báo `esp_https_ota: No option for server verification is enabled...` rồi `esp_https_ota_begin failed: ESP_ERR_INVALID_ARG` → `Status -> HTTPS_CONNECT_FAILED`. Lý do (đọc source `esp-idf/components/esp_https_ota/src/esp_https_ota.c`, hàm `esp_https_ota_begin`): thư viện tự kiểm tra riêng, độc lập với `ota_manager` — thấy `cert_pem == NULL` (đúng vì đang là `http://`) thì tự chặn tiếp bằng `#if CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`, trừ khi option này cũng bật. Tức là 2 lớp guard tách biệt (`ota_manager` của project + `esp_https_ota` của ESP-IDF) đều phải đồng ý thì mới cho qua.
4. Lưu lại: `Q` → `Y` (terminal) hoặc nút Save (VS Code).
5. **Build lại VÀ flash lại thật** — 2 option trên là `#if CONFIG_...` (biên dịch có điều kiện, đóng băng vào firmware lúc build), không phải kiểm tra runtime đọc `sdkconfig` trực tiếp. Chỉ sửa `sdkconfig`/menuconfig mà không build+flash lại thì board đang chạy vẫn là binary cũ, hành vi không đổi:
   ```bash
   idf.py build
   idf.py -p /dev/ttyUSB0 flash
   ```

Trên Windows, chạy thêm server HTTP thuần bên cạnh (dùng chung thư mục `firmware/`, không cần copy `ota.bin` 2 nơi):
```powershell
node server-http.js   # ota_https_server/server-http.js — lang nghe port 8071, KHONG TLS
```

Sau đó chỉ cần gửi lệnh với `url` khác scheme/port, không cần build lại firmware ESP32 lần nữa cho mỗi lần đổi:
```bash
mosquitto_pub -h broker.hivemq.com -t "esp32/vietnq/ota/cmd" \
  -m '{"version":"1.0.2","url":"http://<IP_Windows>:8071/ota.bin"}'
```

**Bước 7 — Tăng Task Watchdog timeout (vì bật bulk erase)**
`idf.py menuconfig` → `Component config` → `ESP System Settings` → `Task Watchdog timeout period (seconds)` → tăng lên `15`.

**Bước 8 — Build và chạy HTTPS server Node.js (trên Windows)**
Yêu cầu: đã cài [Node.js](https://nodejs.org) trên Windows (bản LTS bất kỳ, không cần thư viện ngoài vì `server-https.js` chỉ dùng module built-in).
```bash
idf.py build      # tự động ký nhờ Bước 3-4
# copy build/ota.bin sang máy Windows, vào thư mục ota_https_server/firmware/ota.bin
```
Trên Windows, trong thư mục `ota_https_server/`:
```powershell
node server-https.js
```
Server sẽ log ra dòng xác nhận đang lắng nghe ở cổng 8070, log client vừa bắt tay TLS thành công (IP, phiên bản TLS, cipher), log thanh tiến độ `[====    ] %` khi đang gửi file, và dòng xác nhận gửi xong toàn bộ file — tiện theo dõi khi ESP32 tải firmware. Có thêm `server-http.js` (HTTP thuần, không TLS, port 8071) chỉ dùng khi dev/test — xem mục "Chuyển đổi giữa HTTP và HTTPS" phía trên.

**Bước 9 — Test trigger OTA**
```bash
mosquitto_pub -h broker.hivemq.com -t "esp32/vietnq/ota/cmd" \
  -m '{"version":"1.0.1","url":"https://<IP_Windows>:8070/ota.bin"}'
mosquitto_sub -h broker.hivemq.com -t "esp32/vietnq/ota/status" -v
```
(cần bump version trong `version.txt` lên `1.0.1` và build lại trước khi test, để version-check không bỏ qua).

---

## Việc mình sẽ tự làm (không cần hỏi thêm)

- Viết toàn bộ code trong `components/ota_manager/` + sửa `main/main.c` để gọi vào.
- Viết `ota_https_server/server-https.js` (Node.js, không dependency ngoài) — xem code thật trong file này thay vì chép lại ở đây (đoạn code mẫu ban đầu ở version cũ của tài liệu đã lỗi thời, file thật hiện đã có thêm: log client khi bắt tay TLS thành công (`secureConnection`), thanh tiến độ `[====    ] %` khi gửi file, log khi gửi xong toàn bộ file). Có thêm `server-http.js` (bản HTTP thuần song song, cùng thư mục `firmware/`, xem mục "Chuyển đổi giữa HTTP và HTTPS").
- Tạo `version.txt` mặc định `"1.0.0"`.
- Tạo file `server_certs/ca_cert.pem` placeholder (rỗng/ghi chú) kèm comment rõ cần thay ở Bước 6.
- Tạo sẵn thư mục `ota_https_server/certs/` và `ota_https_server/firmware/` (kèm `README.txt` nhắc lại cần copy gì vào) để bạn copy file thật vào ở Bước 6/8.
- Build thử bằng cấu hình mặc định hiện tại (chưa bật rollback/secure/partition mới) để đảm bảo code compile sạch — phần build "đầy đủ tính năng" chỉ chạy được sau khi bạn hoàn thành các Bước 0–8 ở trên.

## Verification

1. `idf.py build` compile sạch không lỗi/warning liên quan code mới (chạy được ngay cả khi chưa bật Rollback/Secure/OTA-partition, vì code tự kiểm tra `#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` / `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` khi cần).
2. Sau khi bạn hoàn thành Bước 0–8: flash bản `1.0.0` lên board, xác nhận log rollback-confirm xuất hiện sau khi MQTT connect.
3. Publish lệnh OTA qua `mosquitto_pub` (Bước 9) → theo dõi `esp32/vietnq/ota/status` trên `mosquitto_sub` thấy đủ chuỗi trạng thái `OTA_START → ... → REBOOTING`.
4. Sau reboot, `idf.py -p /dev/ttyUSB0 monitor` xác nhận version mới `1.0.1` đang chạy và log rollback-confirm lại xuất hiện (app mới tự xác nhận hợp lệ).
5. Test rollback thật — xem quy trình chi tiết ở mục "Kịch bản test: App Rollback" bên dưới.
6. Test OTA bị từ chối do ký sai key — xem quy trình chi tiết ở mục "Kịch bản test: Firmware ký sai key" bên dưới.

---

## Kịch bản test: App Rollback (firmware mới lỗi → tự quay lại bản cũ)

**Mục tiêu:** mô phỏng "firmware mới có lỗi nghiêm trọng, không bao giờ chạy được tới checkpoint xác nhận" (`ota_rollback_confirm_if_pending()` chỉ được gọi sau khi WiFi + MQTT connect thành công — xem `main/main.c`) → xác nhận bootloader tự động rollback về bản cũ mà **không cần code nào chủ động gọi** `esp_ota_mark_app_invalid_rollback_and_reboot()`.

**Bước 1 — Tạo bản build "cố tình lỗi":**
1. Backup giá trị hiện tại của `WIFI_PASS` trong `components/wifi_manager/WiFi.c` (copy ra chỗ khác để lát revert).
2. Sửa tạm `WIFI_PASS` thành 1 chuỗi sai bất kỳ, ví dụ `"wrong_password_for_rollback_test"` — mục đích: WiFi không bao giờ connect được → không bao giờ tới MQTT_EVENT_CONNECTED → không bao giờ gọi `ota_rollback_confirm_if_pending()`.
3. Sửa `version.txt` thành 1 giá trị rõ ràng là bản test, ví dụ `9.9.9-rollback-test` (tránh nhầm với version thật).
4. `idf.py build`.

**Bước 2 — OTA bản lỗi này lên board (đang chạy bản tốt, ví dụ `1.0.1`):**
```bash
# Copy build/ota.bin (bản 9.9.9-rollback-test) vao ota_https_server/firmware/ota.bin
mosquitto_pub -h broker.hivemq.com -t "esp32/vietnq/ota/cmd" \
  -m '{"version":"9.9.9-rollback-test","url":"https://192.168.2.7:8070/ota.bin"}'
```
Vì file này build/ký bình thường (chỉ sai WiFi password, chữ ký vẫn hợp lệ), OTA vẫn tải + verify + ghi + reboot thành công như mọi lần — quan sát qua `mosquitto_sub` như bình thường tới tận `REBOOTING`.

**Bước 3 — Quan sát firmware lỗi mắc kẹt ở PENDING_VERIFY:**
Sau reboot, **không còn thấy gì trên MQTT nữa** (đúng vì WiFi không connect được) — phải chuyển sang xem qua cáp serial:
```bash
idf.py -p /dev/ttyUSB0 monitor
```
Sẽ thấy log lặp lại vô hạn kiểu:
```
W (...) ota: Wifi disconnected, reason=..., rssi=...
W (...) ota: Retrying wifi connection (1/5)
...
E (...) ota: Failed to connect to wifi after 5 retries, giving up
```
Thiết bị "treo" ở đây mãi mãi — vì `esp_wifi_connect()` không tự lặp lại nữa sau khi hết `WIFI_MAX_RETRY`, nhưng app vẫn đang chạy bình thường (không crash), nên state OTA vẫn nguyên là `PENDING_VERIFY`, chưa ai confirm cũng chưa ai đánh dấu invalid.

**Bước 4 — Ép 1 lần boot mới trong khi vẫn PENDING_VERIFY:**
Nhấn nút **RESET/EN** trên board (hoặc rút/cắm lại nguồn) — đây chính là hành động mô phỏng "mất điện/crash trước khi kịp confirm" mà tài liệu ESP-IDF mô tả.

**Bước 5 — Xác nhận bootloader tự rollback:**
Theo dõi tiếp `idf.py monitor` ngay sau khi board khởi động lại — kỳ vọng thấy:
- Log `log_partition_table()` (in ra ngay đầu `app_main()`) cho thấy **RUNNING đã đổi lại** về đúng partition chứa bản `1.0.1` cũ (không còn là partition vừa OTA lần trước).
- WiFi connect thành công bình thường trở lại (vì đây là bản cũ, `WIFI_PASS` đúng).
- MQTT connect lại, publish version → xác nhận trên `mosquitto_sub -t "esp32/vietnq/version"` thấy **quay về `1.0.1`**, không phải `9.9.9-rollback-test`.
- Log `ota_rollback`: `Running partition "ota_x", OTA state = ...` — state lúc này nên là trạng thái đã VALID từ trước (không phải PENDING_VERIFY), vì đây là bản đã confirm hợp lệ trong lần chạy trước đó.

**Bước 6 — Dọn dẹp sau khi test xong:**
1. Revert `WIFI_PASS` về đúng giá trị thật trong `components/wifi_manager/WiFi.c`.
2. Revert `version.txt` về đúng version đang dùng thật (vd `1.0.1`).
3. `idf.py build`, copy `ota.bin` đúng vào server, OTA lại 1 lần nữa (hoặc `idf.py -p /dev/ttyUSB0 flash` trực tiếp) để dọn sạch bản `9.9.9-rollback-test` còn nằm trong OTA slot kia — không bắt buộc (bản lỗi đã bị "cách ly" ở 1 slot, không ảnh hưởng hoạt động), nhưng nên làm để tránh nhầm lẫn sau này.

---

## Đồng hồ cảnh báo xác nhận rollback (chống treo vô thời hạn nếu WiFi/MQTT không bao giờ kết nối được)

**Lỗ hổng phát hiện được:** checkpoint xác nhận rollback hiện tại (`ota_rollback_confirm_if_pending()`, gọi khi `MQTT_EVENT_CONNECTED`) chỉ có thể chạy nếu WiFi **và** MQTT kết nối thành công. Nhưng `WiFi.c` chỉ retry tối đa `WIFI_MAX_RETRY` (5) lần rồi **bỏ cuộc vĩnh viễn** (xem `KNOWN_ISSUES.md` mục 3) — không tự reboot, không retry thêm. Hệ quả: nếu firmware OTA mới có bug khiến WiFi không bao giờ connect được (nhưng bản thân app không crash), thiết bị sẽ:
- Không bao giờ đạt được checkpoint → không bao giờ confirm.
- Không tự crash → cơ chế rollback-khi-reboot-lại của bootloader **không có cơ hội kích hoạt** (nó chỉ kiểm tra state lúc có 1 lần BOOT MỚI, không theo dõi liên tục).
- Kẹt ở trạng thái `ESP_OTA_IMG_PENDING_VERIFY` **vô thời hạn**, cho tới khi có người rút nguồn/nhấn RESET vật lý.

**Giải pháp:** thêm `ota_rollback_start_confirm_watchdog(uint32_t timeout_ms)` — 1 `esp_timer` one-shot **độc lập với Task Watchdog**, gọi càng sớm càng tốt lúc boot (trong `main.c`, ngay **trước** `wifi_manager_start()`, để đồng hồ tính đủ cả thời gian chờ WiFi retry lẫn MQTT connect):

```c
ota_rollback_start_confirm_watchdog(ROLLBACK_CONFIRM_TIMEOUT_MS);   // 2 phút, main.c gọi trước wifi_manager_start()
wifi_manager_start();
```

Cơ chế:
1. Lúc gọi, tự kiểm tra ngay: nếu **không** đang `PENDING_VERIFY` (boot bình thường, không phải sau OTA) → không làm gì, không tạo timer thừa.
2. Nếu đang `PENDING_VERIFY` → tạo timer, đặt hẹn giờ `timeout_ms`.
3. Hết hạn mà **vẫn chưa confirm** → tự kiểm tra lại state lần nữa (phòng trường hợp confirm vừa xảy ra đúng lúc timer sắp nổ), rồi gọi `esp_ota_mark_app_invalid_rollback_and_reboot()` — **chủ động** rollback + reboot ngay, không chờ ai rút nguồn.
4. `ota_rollback_confirm_if_pending()` confirm thành công → tự `esp_timer_stop()` + `esp_timer_delete()` đồng hồ này ngay, tránh nó nổ vô ích về sau.

**Kết quả — chuỗi hệ quả giờ đã khác:**
```
WiFi bỏ cuộc sau 5 lần retry → MQTT không bao giờ connect → không ai confirm
    → NHƯNG sau đúng 2 phút kể từ boot, ota_rollback tự động:
         esp_ota_mark_app_invalid_rollback_and_reboot() → reboot ngay
    → Bootloader boot lại vào firmware CŨ (đã bị đánh dấu invalid)
    → Không còn treo vô thời hạn, không cần ai rút nguồn thủ công
```

Cần thêm `esp_timer` vào `PRIV_REQUIRES` của `components/ota_manager/CMakeLists.txt`. Giá trị `2 * 60 * 1000` ms (2 phút) là điểm khởi đầu hợp lý (đủ rộng rãi cho cả 5 lần WiFi retry lẫn MQTT connect trong điều kiện mạng bình thường) — **chưa kiểm chứng bằng test thật trên phần cứng**, có thể cần điều chỉnh nếu mạng thực tế cần lâu hơn để ổn định.

---

## Kịch bản test: Firmware OTA bị ký sai key (phải bị từ chối)

**Mục tiêu:** xác nhận `esp_https_ota_finish()` **từ chối** cài đặt nếu file `.bin` được ký bằng khóa khác với khóa đã nhúng vào firmware đang chạy (`secure_boot_signing_key.pem`) — đúng tính năng "Secure OTA Updates Without Secure Boot". Thiết bị phải **giữ nguyên firmware cũ**, publish `SIGNATURE_INVALID`, không reboot.

**Bước 1 — Tạo 1 cặp khóa ký KHÁC (khóa "kẻ giả mạo"), không đụng tới khóa thật đang dùng:**
```bash
cd /home/vietnq/esp/ota
espsecure.py generate_signing_key --version 1 --scheme ecdsa256 wrong_signing_key.pem
```
File `secure_boot_signing_key.pem` thật (đang cấu hình trong menuconfig) **giữ nguyên, không đổi gì** — không cần sửa `sdkconfig`/menuconfig cho bước này.

**Bước 2 — Build bình thường rồi tự ký lại bằng khóa sai, KHÔNG qua `idf.py build`'s auto-sign:**

`idf.py build` vẫn tự động tạo ra **cả 2 file**: `build/ota-unsigned.bin` (chưa ký) và `build/ota.bin` (đã ký bằng khóa thật). Dùng `espsecure.py sign_data` để tự ký thủ công bản unsigned bằng khóa SAI vừa tạo:
```bash
espsecure.py sign_data --version 1 \
  --keyfile wrong_signing_key.pem \
  --output build/ota_wrong_key.bin \
  build/ota-unsigned.bin
```
Kết quả `build/ota_wrong_key.bin` là 1 file **có chữ ký hợp lệ về mặt định dạng** (đúng cấu trúc signature block), nhưng ký bằng khóa mà firmware đang chạy **không nhận diện được**.

**Bước 3 — Đưa file này lên server và trigger OTA:**
```bash
# Copy build/ota_wrong_key.bin sang may Windows, vao ota_https_server/firmware/ota_wrong_key.bin
mosquitto_pub -h broker.hivemq.com -t "esp32/vietnq/ota/cmd" \
  -m '{"version":"9.9.9-wrongkey-test","url":"https://192.168.2.7:8070/ota_wrong_key.bin"}'
```
(Đổi tên file trên server khác `ota.bin` để không lẫn với file thật — code ESP32 lấy đúng theo `url` trong lệnh MQTT nên tên file tùy ý.)

**Bước 4 — Quan sát kết quả mong đợi:**
```bash
mosquitto_sub -h broker.hivemq.com -t "esp32/vietnq/ota/status" -v
```
Kỳ vọng thấy chuỗi trạng thái: `OTA_START → CONNECTED → READING_IMG_DESC → VERIFYING_CHIP_ID → WRITING_FLASH (nhiều lần) → SIGNATURE_INVALID` — **KHÔNG có** `BOOT_PARTITION_UPDATED`, `OTA_FINISH`, hay `REBOOTING`. Log serial tương ứng (`ota_task.c`):
```
E (...) ota_task: Firmware signature invalid, rejecting image
```
Thiết bị vẫn tiếp tục chạy bình thường ở version cũ — publish lại `esp32/vietnq/ota/cmd` lần nữa với file ký đúng vẫn hoạt động được ngay (vì `ota_manager_notify_task_done()` đã được gọi ở nhánh lỗi này, giải phóng cờ `s_ota_in_progress`).

**Bước 5 — Dọn dẹp:** xoá `wrong_signing_key.pem` và `build/ota_wrong_key.bin` nếu không cần giữ lại, không cần revert gì khác (không có config nào bị thay đổi ở kịch bản này).

---

## Sự cố đã gặp khi test thật

### Guru Meditation Error (LoadStoreError) trong lúc OTA — do `buffer_caps` thiếu `MALLOC_CAP_8BIT`

**Log lỗi (xảy ra 2 lần, kể cả sau khi thử tắt `bulk_flash_erase`):**
```
I (18625) esp_https_ota: Writing to <ota_0> partition at offset 0x110000
I (18625) ota_task: Downloading firmware version: 1.0.1, image size: 983028 bytes
Guru Meditation Error: Core  1 panic'ed (LoadStoreError). Exception was unhandled.
...
Backtrace: ... esp_event_post_to → esp_event_post → esp_https_ota_dispatch_event
            → esp_ota_verify_chip_id → esp_https_ota_perform → ota_task
```

**⚠️ Chẩn đoán lần 1 (SAI, đã đính chính):** ban đầu nghi ngờ `bulk_flash_erase = true` (erase toàn bộ partition 1 lượt) làm rối loạn timing WiFi/event loop → đã thử tắt về `false` → **crash vẫn xảy ra y hệt, cùng địa chỉ lỗi (`EXCVADDR: 0x40098358`)** ở lần test tiếp theo → chứng tỏ erase không phải nguyên nhân, phải tìm lại.

**Nguyên nhân thật sự:** dòng cấu hình `.buffer_caps = MALLOC_CAP_INTERNAL` (thiếu `MALLOC_CAP_8BIT`). Theo `esp_heap_caps.h`:
```c
#define MALLOC_CAP_32BIT    (1<<1)   // Memory must allow for aligned 32-bit data accesses
#define MALLOC_CAP_8BIT     (1<<2)   // Memory must allow for 8/16/...-bit data accesses
#define MALLOC_CAP_INTERNAL (1<<11)  // chi nghia la "vung nho noi bo" - KHONG lien quan gi den 8-bit hay 32-bit
```
`MALLOC_CAP_INTERNAL` một mình không đảm bảo vùng nhớ trả về hỗ trợ truy cập theo byte — bộ cấp phát được phép trả về **IRAM** (vùng chứa code thực thi, trên kiến trúc Xtensa chỉ hỗ trợ đọc/ghi 32-bit). Buffer OTA (`ota_upgrade_buf`) vô tình rơi vào IRAM; ngay khi `esp_https_ota` đọc trường `chip_id` (kiểu `uint16_t`, 2 byte, nằm ở offset không chia hết cho 4 trong `esp_image_header_t`) để verify chip ID, phép đọc theo byte này gây `LoadStoreError` — khớp chính xác với vị trí crash trong backtrace.

**Cách sửa:** đổi thành `.buffer_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` — buộc bộ cấp phát chỉ trả về DRAM thường (hỗ trợ byte-access), không bao giờ là IRAM. Đồng thời bật lại `bulk_flash_erase = true` (vì đã xác nhận nó không liên quan tới crash, không có lý do gì phải hy sinh tốc độ).

**Bài học:** khi dùng `heap_caps_malloc()`/`buffer_caps` với dữ liệu sẽ bị truy cập theo byte (struct C thông thường, không phải mảng `uint32_t` thuần), **luôn phải có `MALLOC_CAP_8BIT`** — `MALLOC_CAP_INTERNAL` không tự bao hàm điều đó. Đây là lỗi dễ mắc và dễ nhầm sang nguyên nhân khác (như erase timing) vì exception xảy ra ở 1 hàm thư viện xa chỗ thực sự cấp phát buffer.

**✅ Đã test thật trên board và xác nhận fix đúng:** OTA chạy trọn vẹn từ `1.0.0` → `1.0.1`, không còn crash; sau reboot thiết bị publish version `1.0.1` lên `esp32/vietnq/version`, khớp đúng luồng mô tả ở mục Verification.

---

## Tổng hợp toàn bộ topic MQTT

Broker đang dùng: `broker.hivemq.com:1883` (plain MQTT, không TLS — xem giải thích ở phần trước).

| Topic | Chiều | Payload mẫu | Tác dụng | Định nghĩa ở |
|---|---|---|---|---|
| `esp32/vietnq/topic1` | Broker → Device (subscribe) | tùy nội dung publish lên | Topic test chung, thiết bị chỉ log ra nội dung + tên topic nhận được, không xử lý logic gì | `main/main.c` (`MQTT_TOPIC_1`) |
| `esp32/vietnq/topic2` | Broker → Device (subscribe) | tùy nội dung publish lên | Giống `topic1`, topic test thứ 2 | `main/main.c` (`MQTT_TOPIC_2`) |
| `esp32/vietnq/version` | Device → Broker (publish) | `{"version":"1.0.0"}` | Bắn 1 lần ngay khi thiết bị kết nối MQTT thành công, báo version firmware đang chạy | `main/main.c` (`VERSION_TOPIC`), đọc version qua `esp_app_get_description()` |
| `esp32/vietnq/ota/cmd` | Broker → Device (subscribe) | `{"version":"1.0.1","url":"https://192.168.2.7:8070/ota.bin"}` | Lệnh yêu cầu bắt đầu OTA — thiết bị chỉ thực thi nếu `version` mới hơn version đang chạy và không có lượt OTA nào đang dở | `components/ota_manager/include/ota_manager.h` (`OTA_COMMAND_TOPIC`), xử lý ở `ota_manager.c` |
| `esp32/vietnq/ota/status` | Device → Broker (publish) | `{"status":"WRITING_FLASH","progress":45231}` | Báo cáo từng bước tiến trình OTA (xem đầy đủ 9 trạng thái + 2 trạng thái lỗi ở bảng "Các trạng thái publish lên MQTT trong lúc OTA" phía trên) | `components/ota_manager/include/ota_manager.h` (`OTA_STATUS_TOPIC`), publish ở `ota_task.c` |

**Lệnh test nhanh (mosquitto):**
```bash
# Theo dõi tất cả topic của project cùng lúc
mosquitto_sub -h broker.hivemq.com -t "esp32/vietnq/#" -v

# Trigger OTA thủ công
mosquitto_pub -h broker.hivemq.com -t "esp32/vietnq/ota/cmd" \
  -m '{"version":"1.0.1","url":"https://192.168.2.7:8070/ota.bin"}'

# Gửi thử bản tin test
mosquitto_pub -h broker.hivemq.com -t "esp32/vietnq/topic1" -m "hello"
```
