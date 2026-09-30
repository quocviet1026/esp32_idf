# Xây dựng tính năng OTA trên ESP32 — Tài liệu triển khai chi tiết

> Yêu cầu: Safe update mode · App Rollback · Secure OTA Without Secure Boot ·
> Tuning Performance · HTTPS local server · Bảo mật đường truyền · Giám sát trạng thái qua MQTT
>
> **Bản này đã LOẠI BỎ tính năng Anti-rollback** so với bản gốc `esp32-ota-full-implementation-plan.md`.

---

## 1. Tổng quan kiến trúc

### 1.1. Bảng ánh xạ yêu cầu → cơ chế/example nền tảng

| Yêu cầu | Cơ chế | Example tham khảo chính |
|---|---|---|
| Safe update mode | Partition table 2 OTA slot (A/B) | `CONFIG_PARTITION_TABLE_TWO_OTA` |
| App Rollback | `esp_ota_mark_app_valid_cancel_rollback()` / `_invalid_...` | [`native_ota_example`](https://github.com/espressif/esp-idf/tree/v6.1/examples/system/ota/native_ota_example) |
| Secure OTA Without Secure Boot | `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` + `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT` | [Secure Boot v1 — Signed App Verify](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/security/secure-boot-v1.html#signed-app-verify) |
| Tuning Performance | `esp_https_ota_config_t`: `bulk_flash_erase`, `buffer_size`, `buffer_caps` | [`advanced_https_ota`](https://github.com/espressif/esp-idf/tree/master/examples/system/ota/advanced_https_ota) |
| HTTPS local server | Self-signed cert + `esp_http_client`/`esp_https_ota` | README chung OTA examples |
| Bảo mật đường truyền | Chính HTTPS (TLS) đã đảm nhiệm — **không cần** `pre_encrypted_ota` vì đã dùng HTTPS | (khác với bản dùng HTTP thuần) |
| Giám sát trạng thái qua MQTT | Event Handler của `esp_https_ota` (`ESP_HTTPS_OTA_EVENT`) + `esp-mqtt` client publish | [`advanced_https_ota`](https://github.com/espressif/esp-idf/tree/master/examples/system/ota/advanced_https_ota) + [`protocols/mqtt/tcp`](https://github.com/espressif/esp-idf/tree/master/examples/protocols/mqtt/tcp) |
| **Trigger OTA** | Subscribe MQTT topic `ota/command` — server chủ động ra lệnh, thiết bị **không** tự OTA mỗi lần boot | Thiết kế riêng, xem Bước 5 |

### 1.2. Vì sao KHÔNG cần `pre_encrypted_ota` lần này

Ở bản yêu cầu trước (dùng HTTP thuần), cần `pre_encrypted_ota` để tự mã hóa firmware vì kênh truyền không có TLS. Lần này bạn đã chọn **HTTPS** — bản thân TLS đã cung cấp:
- **Confidentiality** (mã hóa đường truyền) — thay thế vai trò của `decrypt_cb`.
- **Integrity** (chống sửa đổi giữa đường) — do TLS tự đảm bảo qua MAC/AEAD.

→ Kiến trúc lần này **đơn giản hơn**: chỉ cần ghép **2 example** (`advanced_https_ota` + `native_ota_example`) thay vì 3, cộng thêm 1 component MQTT.

### 1.3. Sơ đồ kiến trúc tổng thể

```
┌─────────────────┐         HTTPS (TLS)          ┌──────────────────┐
│  Local HTTPS     │◄─────────────────────────────│      ESP32       │
│  Server          │   tải firmware đã ký số       │  (advanced_https_ │
│  (self-signed    │                                │   ota + rollback) │
│   cert)          │                                │                   │
└─────────────────┘                                └─────────┬─────────┘
                                                               │ MQTT publish
                                                               │ trạng thái
                                                     ┌─────────▼─────────┐
                                                     │  MQTT Broker      │
                                                     │  local (mosquitto)│
                                                     └─────────┬─────────┘
                                                               │
                                                     ┌─────────▼─────────┐
                                                     │  Dashboard/        │
                                                     │  MQTT subscriber   │
                                                     │  giám sát          │
                                                     └────────────────────┘
```

---

## 2. Chuẩn bị môi trường

```bash
# Lấy ESP-IDF (khuyến nghị bản v6.1 trở lên, đồng bộ với các phần đã tham khảo)
git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh && . ./export.sh
```

---

## 3. Cách lấy Example chính thức của hãng ra để dùng

Có 3 cách, tùy vào việc bạn muốn **chỉ mượn tham khảo** hay **copy hẳn ra làm project riêng**.

### 3.1. Cách 1 — Dùng trực tiếp bản đã có sẵn trong ESP-IDF đã clone (chỉ để đọc/chạy thử, không tách riêng)

Sau khi `git clone` ESP-IDF ở Bước 2, mọi example đã nằm sẵn trong `$IDF_PATH/examples/`:

```bash
echo $IDF_PATH   # xác nhận biến môi trường đã export đúng
cd $IDF_PATH/examples/system/ota/advanced_https_ota
idf.py set-target esp32
idf.py menuconfig
idf.py build flash monitor
```

⚠️ **Nhược điểm**: build/sửa trực tiếp trong `$IDF_PATH/examples` dễ bị lẫn với source ESP-IDF gốc, và nếu sau này `git pull` cập nhật ESP-IDF, thay đổi của bạn có thể bị ghi đè/xung đột. Chỉ nên dùng cách này để **chạy thử nhanh**, không dùng để phát triển lâu dài.

### 3.2. Cách 2 — `idf.py create-project-from-example` (khuyến nghị, dùng cho project chính thức)

Đây là cách **chính thức** để copy 1 example ra thành project độc lập, tại bất kỳ thư mục nào bạn muốn, không phụ thuộc `$IDF_PATH`:

```bash
mkdir -p ~/my_ota_project && cd ~/my_ota_project

# Cú pháp: idf.py create-project-from-example "espressif/esp-idf:<đường dẫn example>"
idf.py create-project-from-example "espressif/esp-idf:system/ota/advanced_https_ota"

cd advanced_https_ota
idf.py set-target esp32
```

**Lấy nhiều example khác cùng lúc**, mỗi cái 1 thư mục riêng, để tham khảo song song:

```bash
idf.py create-project-from-example "espressif/esp-idf:system/ota/native_ota_example"
idf.py create-project-from-example "espressif/esp-idf:system/ota/otatool"
idf.py create-project-from-example "espressif/esp-idf:protocols/mqtt/tcp"
```

Lệnh này tự động lấy đúng phiên bản example **khớp với version ESP-IDF đang active** trong `$IDF_PATH` — tránh trường hợp lấy nhầm example của bản ESP-IDF khác không tương thích API.

### 3.3. Cách 3 — Copy thủ công bằng `cp` (khi cần chỉnh sửa ngay, không qua công cụ)

```bash
cp -r $IDF_PATH/examples/system/ota/advanced_https_ota ~/my_ota_project
cd ~/my_ota_project
rm -rf build sdkconfig sdkconfig.old   # dọn sạch artifact build cũ nếu có
idf.py set-target esp32
```

Cách này tương đương Cách 2 về kết quả, chỉ khác là làm thủ công — hữu ích khi máy không có kết nối mạng để `idf.py` tự fetch metadata.

### 3.4. Lấy example không nằm trong ESP-IDF gốc (VD: `esp_encrypted_img`, các component ngoài)

Một số thứ (như `esp_encrypted_img` đã nhắc ở phần trước) là **component ngoài**, không nằm trong repo `esp-idf` chính mà nằm ở [ESP Component Registry](https://components.espressif.com/):

```bash
# Thêm component vào project hiện tại
idf.py add-dependency "espressif/esp_encrypted_img"

# Hoặc tìm example đi kèm ngay trên trang component registry
# → https://components.espressif.com/components/espressif/esp_encrypted_img
#   → tab "Examples" → tải riêng thư mục example đó về
```

### 3.5. Bảng tra nhanh nguồn của từng example đã dùng trong tài liệu này

| Example | Nguồn |
|---|---|
| `system/ota/advanced_https_ota` | Trong repo `esp-idf` chính, `examples/system/ota/` |
| `system/ota/native_ota_example` | Trong repo `esp-idf` chính |
| `system/ota/otatool` | Trong repo `esp-idf` chính |
| `protocols/mqtt/tcp`, `protocols/mqtt/ssl` | Trong repo `esp-idf` chính, `examples/protocols/mqtt/` |
| Ký số app (`espsecure.py`) | Đi kèm sẵn trong `esp-idf` (không phải "example" mà là công cụ CLI) |

→ Với toàn bộ example dùng trong tài liệu này, **Cách 2 (`create-project-from-example`) là đủ** — không cần đụng tới Component Registry.

---

## 4. Khởi tạo project và thêm dependency

```bash
cd ~/my_ota_project/advanced_https_ota

# Thêm component MQTT (nếu chưa có sẵn trong managed_components)
idf.py add-dependency "espressif/esp-mqtt"
```

---

## 5. Bước 1 — Thiết kế Partition Table (Safe Update Mode)

Dùng partition table 2 OTA slot có sẵn:

```
Kconfig: CONFIG_PARTITION_TABLE_TWO_OTA=y
```

Hoặc tự định nghĩa `partitions.csv` tùy chỉnh nếu cần kiểm soát offset/size:

```csv
# Name,   Type, SubType, Offset,   Size,
nvs,      data, nvs,     0x9000,   0x6000,
otadata,  data, ota,     0xf000,   0x2000,
factory,  app,  factory, 0x10000,  1M,
ota_0,    app,  ota_0,   ,         1M,
ota_1,    app,  ota_1,   ,         1M,
```

> Vì **không dùng Anti-rollback** ở bản này, partition `factory` được giữ nguyên bình thường, không có ràng buộc gì thêm — vẫn hữu ích làm "phao cứu sinh" khi cần factory reset.

---

## 6. Bước 2 — Cấu hình Kconfig tổng hợp

```bash
idf.py menuconfig
```

Bật các mục sau:

```
Bootloader config
  [*] Enable app rollback support                    → CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE

Security features
  [*] Require signed app images                       → CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT
  [*]   Verify app signature on update                 → CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT

Partition Table
  (X) Two OTA definitions                              → CONFIG_PARTITION_TABLE_TWO_OTA
```

---

## 7. Bước 3 — Ký số firmware (Secure OTA Without Secure Boot)

### 7.1. Tạo cặp khóa ký (RSA)

```bash
espsecure.py generate_signing_key --version 1 signing_key.pem
```

### 7.2. Cấu hình ESP-IDF dùng đúng khóa này khi build

```
idf.py menuconfig
→ Security features → Signing key → chọn file signing_key.pem
```

Với cấu hình này, **mỗi lần `idf.py build`**, ESP-IDF **tự động ký** app image bằng khóa trên — không cần bước thủ công riêng.

### 7.3. Cơ chế xác thực khi OTA

Vì đã bật `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`, `esp_https_ota_finish()` sẽ **tự động verify chữ ký** của image mới tải về bằng public key đã nhúng sẵn trong app đang chạy (không cần code thêm gì) — nếu chữ ký sai, trả về `ESP_ERR_OTA_VALIDATE_FAILED`.

> Tham khảo chi tiết: [Signed App Verification Without Hardware Secure Boot](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/security/secure-boot-v1.html#signed-app-verify)

---

## 8. Bước 4 — Tự xây dựng HTTPS Server local

### 8.1. Xác định IP local của máy tính chạy server

```bash
# Linux/macOS
ip addr show | grep "inet "          # hoặc: ifconfig
# Windows
ipconfig
```

Ghi lại địa chỉ dạng `192.168.x.x` (cùng dải mạng với ESP32) — đây sẽ là giá trị dùng cho **CN của certificate** và **URL trong firmware**.

### 8.2. Tạo cấu trúc thư mục project cho server

```bash
mkdir -p ~/ota_https_server/{certs,firmware}
cd ~/ota_https_server
```

```
ota_https_server/
├── certs/
│   ├── ca_key.pem      ← private key (KHÔNG bao giờ đưa vào firmware)
│   └── ca_cert.pem     ← public cert (SẼ nhúng vào firmware)
├── firmware/
│   └── app_signed.bin  ← firmware đã build + ký ở Bước 3
└── ota_server.py       ← script chạy server
```

### 8.3. Tạo self-signed certificate

```bash
cd certs
openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365 -nodes \
  -subj "/CN=192.168.1.100"
cd ..
```

- `-nodes`: không mã hóa private key bằng passphrase (tiện cho server tự động khởi động lại, chỉ dùng nội bộ/test).
- `/CN=192.168.1.100`: thay bằng đúng IP đã lấy ở Bước 8.1 — **bắt buộc khớp**, nếu không ESP32 sẽ từ chối vì CN không trùng host đang kết nối.
- `-days 365`: hạn dùng 1 năm — đủ cho vòng đời dự án test, tạo lại khi hết hạn.

### 8.4. Viết script server (`ota_server.py`)

```python
#!/usr/bin/env python3
"""HTTPS server phục vụ firmware OTA cho ESP32 — chỉ dùng nội bộ/test."""
import http.server
import ssl
import os

HOST = "0.0.0.0"
PORT = 8070
CERT_DIR = "certs"
SERVE_DIR = "firmware"

os.chdir(SERVE_DIR)  # server chỉ serve nội dung trong thư mục firmware/

handler = http.server.SimpleHTTPRequestHandler
httpd = http.server.HTTPServer((HOST, PORT), handler)

ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ssl_context.load_cert_chain(
    certfile=os.path.join("..", CERT_DIR, "ca_cert.pem"),
    keyfile=os.path.join("..", CERT_DIR, "ca_key.pem"),
)
httpd.socket = ssl_context.wrap_socket(httpd.socket, server_side=True)

print(f"HTTPS OTA server đang chạy tại: https://<IP-của-bạn>:{PORT}/")
print(f"Đang serve thư mục: {os.getcwd()}")
httpd.serve_forever()
```

> Lưu ý: dùng `ssl.SSLContext` + `load_cert_chain()` (API hiện đại) thay vì `ssl.wrap_socket()` trực tiếp — hàm cũ này đã bị loại bỏ (deprecated/removed) từ Python 3.12 trở lên.

### 8.5. Chạy server — Cách A: dùng `openssl s_server` (cách chính thức, đơn giản nhất)

Đây là cách **được chính README gốc của ESP-IDF OTA examples khuyến nghị** — không cần viết script gì cả:

```bash
cd firmware/
openssl s_server -WWW -key ../certs/ca_key.pem -cert ../certs/ca_cert.pem -port 8070
```

- `-WWW`: bật chế độ mini HTTP server tích hợp sẵn trong `openssl`, tự serve file tĩnh trong thư mục hiện tại.
- Không cần cài Python hay viết code gì thêm — chỉ cần có sẵn `openssl` (thường có sẵn trên Linux/macOS; trên Windows cần thêm `winpty` phía trước lệnh nếu dùng Git Bash/MSYS2).

### 8.5b. Chạy server — Cách B: tự viết script Python (khi cần tùy biến thêm, VD: log request, xử lý nhiều file)

```bash
python3 ota_server.py
# → HTTPS OTA server đang chạy tại: https://<IP-của-bạn>:8070/
# → Đang serve thư mục: /home/user/ota_https_server/firmware
```

### 8.6. Kiểm tra server hoạt động đúng (TRƯỚC KHI thử với ESP32)

```bash
# Từ chính máy chạy server, hoặc máy khác cùng LAN
curl -k https://192.168.1.100:8070/app_signed.bin -o /tmp/test_download.bin

# So sánh checksum để chắc chắn tải đúng, không bị hỏng giữa đường
sha256sum firmware/app_signed.bin
sha256sum /tmp/test_download.bin
```

`-k` bỏ qua việc verify cert (vì là self-signed, curl không tin tưởng theo mặc định) — chỉ dùng để test thủ công, **không liên quan** tới việc ESP32 verify cert (ESP32 verify qua cert đã nhúng ở Bước 6.2/8.7 bên dưới, cơ chế riêng).

### 8.7. Nhúng cert vào firmware ESP32

`main/CMakeLists.txt`:
```cmake
idf_component_register(...
    EMBED_TXTFILES server_certs/ca_cert.pem
)
```

Copy đúng file `certs/ca_cert.pem` (chỉ phần **public cert**, không copy `ca_key.pem`) vào `main/server_certs/ca_cert.pem` của project ESP32.

Trong code:
```c
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[]   asm("_binary_ca_cert_pem_end");
```

### 8.8. Cấu hình ESP32 trỏ đúng tới server

```c
esp_http_client_config_t http_config = {
    .url = "https://192.168.1.100:8070/app_signed.bin",
    .cert_pem = (char *)server_cert_pem_start,
};
```

### 8.9. Xử lý lỗi CN/SAN mismatch khi test (nếu gặp)

> **Đính chính**: đây **không phải** một tùy chọn trong `idf.py menuconfig`. Đã rà soát lại và xác nhận không tồn tại menu path "Component config → ESP-TLS → Skip server certificate CN fieldcheck" trong ESP-IDF chính thức — đây là nhầm lẫn cần sửa. Cơ chế đúng nằm **ở tầng code**, qua field `skip_cert_common_name_check` trong struct `esp_http_client_config_t`:

```c
esp_http_client_config_t http_config = {
    .url = "https://192.168.1.100:8070/app_signed.bin",
    .cert_pem = (char *)server_cert_pem_start,
    .skip_cert_common_name_check = true,   // Chỉ bỏ qua check CN, vẫn verify chain/CA bình thường
};
```

Field này được mô tả chính thức là: *"Skip any validation of server certificate CN field. Pointer to the string containing server certificate common name. If non-NULL, server certificate CN must match this name, if NULL, server certificate CN must match hostname."*

**Phân biệt rõ 2 mức độ — dễ nhầm lẫn:**

| Cơ chế | Tác dụng | Mức độ rủi ro |
|---|---|---|
| `skip_cert_common_name_check = true` (code, field trên) | Chỉ bỏ qua so khớp **CN** với hostname, **vẫn verify** cả chuỗi cert (chain) đúng CA | An toàn hơn, phù hợp test local khi CN không khớp IP |
| `CONFIG_ESP_TLS_INSECURE` + `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY` (Kconfig thật, `idf.py menuconfig → Component config → ESP-TLS`) | Bỏ qua **hoàn toàn** việc verify server certificate | Rủi ro cao — dễ bị man-in-the-middle, **chỉ dùng khi thực sự cần test nhanh, không mang lên production** |

⚠️ **Khuyến nghị**: dùng `skip_cert_common_name_check = true` là đủ cho hầu hết trường hợp test local (đã tạo cert đúng CN ở Bước 8.3 thì thực ra không cần field này nữa — chỉ cần khi bạn đổi IP máy chủ mà chưa tạo lại cert). Tuyệt đối tránh dùng `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY` ngoài mục đích debug nhanh, và **không để sót lại** trong bản build production.

### 8.10. Quy trình cập nhật khi có bản build mới

```bash
# Sau khi build + ký firmware mới ở Bước 3
cp build/app_signed.bin ~/ota_https_server/firmware/app_signed.bin
# Server đang chạy sẵn sẽ TỰ ĐỘNG serve file mới — không cần restart script
```

---

## 9. Bước 5 — Dựng MQTT Broker local & tích hợp client

### 9.1. Dựng broker local (Mosquitto)

```bash
# Linux
sudo apt install mosquitto mosquitto-clients
sudo systemctl start mosquitto

# Kiểm tra hoạt động
mosquitto_sub -h localhost -t "ota/#" -v
```

### 9.2. Thêm component MQTT vào project

```bash
idf.py add-dependency "espressif/esp-mqtt"
```

Tham khảo cấu hình chuẩn tại example chính thức: [`protocols/mqtt/tcp`](https://github.com/espressif/esp-idf/tree/master/examples/protocols/mqtt/tcp) (kết nối cơ bản, không TLS — phù hợp broker local) hoặc [`protocols/mqtt/ssl`](https://github.com/espressif/esp-idf/tree/master/examples/protocols/mqtt/ssl) nếu muốn bảo mật luôn kênh MQTT.

### 9.3. Thiết kế cơ chế trigger — MQTT Command (server chủ động ra lệnh)

**Nguyên tắc**: thiết bị **không tự động OTA mỗi lần boot**. Nó chỉ boot bình thường, kết nối MQTT, **subscribe** 1 topic lệnh, rồi **chờ**. Chỉ khi server publish 1 message lên đúng topic đó, thiết bị mới bắt đầu tải.

**Thiết kế topic:**

| Topic | Chiều | Payload mẫu | Vai trò |
|---|---|---|---|
| `ota/command` | Server → Device | `{"version":"1.2.0","url":"https://192.168.1.100:8070/app_signed.bin"}` | Server ra lệnh "có bản mới" |
| `ota/status` | Device → Server | `{"status":"WRITING_FLASH","progress":45}` | Thiết bị báo trạng thái (đã có ở phần Event Handler) |

**Guard chống trigger trùng/lặp vô hạn — 2 lớp bảo vệ:**
1. Cờ `ota_in_progress` (biến static, `volatile bool`) — nếu đang OTA dở, bỏ qua mọi lệnh mới đến.
2. **So sánh version** — chỉ tải nếu `version` trong lệnh MQTT **lớn hơn** version app đang chạy (đọc qua `esp_ota_get_partition_description()`). Đây chính là điểm khắc phục lỗi ở luồng cũ (tự OTA lại y hệt file sau mỗi lần reboot).

### 9.4. Code MQTT client hoàn chỉnh (thay thế bản cũ)

```c
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_ota_ops.h"

static esp_mqtt_client_handle_t mqtt_client;
static volatile bool ota_in_progress = false;

// forward declare, định nghĩa đầy đủ ở Bước 7
void start_ota_task(const char *url);

static bool is_version_newer(const char *incoming_version)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_desc;
    if (esp_ota_get_partition_description(running, &running_desc) != ESP_OK) {
        return false;   // không đọc được version hiện tại → an toàn, không OTA
    }
    // So sánh chuỗi đơn giản; có thể thay bằng semver compare nếu cần chính xác hơn
    return strcmp(incoming_version, running_desc.version) != 0;
}

static void mqtt_data_event_handler(esp_mqtt_event_handle_t event)
{
    if (strncmp(event->topic, "ota/command", event->topic_len) != 0) return;

    char payload[256] = {0};
    int len = event->data_len < sizeof(payload) - 1 ? event->data_len : sizeof(payload) - 1;
    memcpy(payload, event->data, len);

    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) return;

    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *url     = cJSON_GetObjectItem(root, "url");

    if (cJSON_IsString(version) && cJSON_IsString(url)) {
        if (ota_in_progress) {
            ESP_LOGW(TAG, "OTA đang chạy dở, bỏ qua lệnh mới");
        } else if (!is_version_newer(version->valuestring)) {
            ESP_LOGI(TAG, "Version %s không mới hơn hiện tại, bỏ qua", version->valuestring);
        } else {
            ota_in_progress = true;
            start_ota_task(url->valuestring);   // ← ĐIỂM TRIGGER DUY NHẤT
        }
    }
    cJSON_Delete(root);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_subscribe(mqtt_client, "ota/command", 1);   // QoS 1
            break;
        case MQTT_EVENT_DATA:
            mqtt_data_event_handler(event);
            break;
        default:
            break;
    }
}

void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://192.168.1.100:1883",   // broker local
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void ota_publish_status(const char *status, int progress) {
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"status\":\"%s\",\"progress\":%d}", status, progress);
    esp_mqtt_client_publish(mqtt_client, "ota/status", payload, 0, 1, 0);
}
```

> Component `cJSON` đã có sẵn trong ESP-IDF (`json` component), không cần `add-dependency` thêm.

---

## 10. Bước 6 — Ghép Event Handler của `advanced_https_ota` với MQTT publish

Đây là phần lõi kết nối OTA ↔ MQTT, dựa trên cơ chế event của `esp_https_ota`:

```c
static void ota_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base != ESP_HTTPS_OTA_EVENT) return;

    switch (event_id) {
        case ESP_HTTPS_OTA_START:
            ota_publish_status("OTA_START", 0);
            break;
        case ESP_HTTPS_OTA_CONNECTED:
            ota_publish_status("CONNECTED", 0);
            break;
        case ESP_HTTPS_OTA_GET_IMG_DESC:
            ota_publish_status("READING_IMG_DESC", 0);
            break;
        case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
            ota_publish_status("VERIFYING_CHIP_ID", 0);
            break;
        case ESP_HTTPS_OTA_WRITE_FLASH: {
            int written = *(int*)event_data;
            ota_publish_status("WRITING_FLASH", written);
            break;
        }
        case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
            ota_publish_status("BOOT_PARTITION_UPDATED", 100);
            break;
        case ESP_HTTPS_OTA_FINISH:
            ota_publish_status("OTA_FINISH", 100);
            break;
        case ESP_HTTPS_OTA_ABORT:
            ota_publish_status("OTA_ABORT", -1);
            break;
    }
}

// Đăng ký trong app_main(), SAU KHI mqtt_app_start()
esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, ota_event_handler, NULL);
```

⚠️ **Thứ tự khởi tạo quan trọng**: phải gọi `mqtt_app_start()` và đảm bảo kết nối MQTT **ổn định trước khi** bắt đầu tác vụ OTA, nếu không các message trạng thái đầu tiên (`OTA_START`) có thể bị rớt do client MQTT chưa kết nối xong.

---

## 11. Bước 7 — Luồng OTA chính (dựa trên `advanced_https_ota` + Tuning Performance)

Khác với bản trước, `ota_task` giờ **nhận URL làm tham số** — được truyền vào **duy nhất** từ điểm trigger MQTT ở Bước 5, không tự chạy độc lập.

```c
#include "esp_https_ota.h"

void start_ota_task(const char *url)
{
    char *url_copy = strdup(url);   // task tự sở hữu bản copy, free khi xong
    xTaskCreate(&ota_task, "ota_task", 8192, url_copy, 5, NULL);
}

static void ota_task(void *pvParameter)
{
    char *url = (char *)pvParameter;

    esp_http_client_config_t http_config = {
        .url = url,
        .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .bulk_flash_erase = true,              // ← Tuning Performance
        .buffer_caps = MALLOC_CAP_INTERNAL,    // ← Tuning Performance (nếu có PSRAM)
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ota_publish_status("BEGIN_FAILED", -1);
        goto ota_task_end;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    ESP_LOGI(TAG, "Dang OTA len version: %s", app_desc.version);   // chỉ để log, không còn check secure_version

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        // esp_https_ota_perform() TỰ ĐỘNG trigger event ESP_HTTPS_OTA_WRITE_FLASH
        // → MQTT publish tiến trình đã xử lý ở Event Handler (Bước 6)
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        ota_publish_status("INCOMPLETE_DATA", -1);
        esp_https_ota_abort(https_ota_handle);
        goto ota_task_end;
    }

    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
    if (ota_finish_err == ESP_OK) {
        ota_publish_status("REBOOTING", 100);
        free(url);
        esp_restart();   // không bao giờ tới dòng dưới
    } else {
        if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ota_publish_status("SIGNATURE_INVALID", -1);   // Bảo mật: chữ ký sai
        }
    }

ota_task_end:
    free(url);
    ota_in_progress = false;   // ← QUAN TRỌNG: mở lại cờ để lệnh MQTT lần sau còn trigger được
    vTaskDelete(NULL);
}
```

⚠️ **Không quên `ota_in_progress = false`** ở mọi nhánh thoát **trừ** nhánh thành công (vì nhánh thành công dẫn tới `esp_restart()`, cờ tự nhiên "reset" vì cả RAM bị xóa khi reboot). Nếu quên dòng này ở các nhánh lỗi, thiết bị sẽ **kẹt vĩnh viễn** không nhận được lệnh OTA nào nữa cho tới khi tự reboot bằng cách khác.

---

## 12. Bước 8 — App Rollback

### Self-test & confirm ngay đầu `app_main()` — ĐÃ SỬA: bỏ trigger vô điều kiện

```c
#include "esp_ota_ops.h"

void app_main(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // Khởi tạo MQTT SỚM để có thể publish trạng thái confirm
            mqtt_app_start();
            bool diagnostic_is_ok = diagnostic();   // tự viết self-test
            if (diagnostic_is_ok) {
                esp_ota_mark_app_valid_cancel_rollback();
                ota_publish_status("APP_CONFIRMED_VALID", 100);
            } else {
                ota_publish_status("APP_INVALID_ROLLING_BACK", -1);
                esp_ota_mark_app_invalid_rollback_and_reboot();
                return;   // esp_restart() bên trong hàm trên đã reboot, dòng này không tới
            }
        }
    }

    esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, ota_event_handler, NULL);

    if (mqtt_client == NULL) {
        mqtt_app_start();   // nếu chưa khởi tạo ở nhánh Rollback phía trên
    }

    // KHÔNG còn xTaskCreate(&ota_task, ...) ở đây.
    // Thiết bị giờ chỉ NGỒI CHỜ lệnh MQTT trên topic "ota/command"
    // (xem mqtt_data_event_handler ở Bước 5) — start_ota_task() chỉ được
    // gọi từ ĐÚNG 1 nơi: bên trong handler đó, khi có lệnh hợp lệ tới.
}
```

**Luồng trigger đầy đủ sau khi sửa:**

```
Boot (bất kỳ lý do gì: power-on, reboot sau OTA, watchdog...)
        │
Check Rollback (chỉ hành động nếu state == PENDING_VERIFY)
        │
Đăng ký Event Handler (ESP_HTTPS_OTA_EVENT) — sẵn sàng, chưa làm gì
        │
mqtt_app_start() → kết nối broker → subscribe "ota/command"
        │
app_main() KẾT THÚC — thiết bị chạy các task bình thường khác,
KHÔNG có OTA nào diễn ra
        │
        ▼ (chờ vô thời hạn — có thể vài phút, vài ngày...)
        │
Server publish message lên "ota/command"
        │
mqtt_data_event_handler() nhận được, parse JSON
        │
   ota_in_progress? → Có: bỏ qua
        │
   Không → version mới hơn? → Không: bỏ qua
        │
   Có → ota_in_progress = true → start_ota_task(url)
        │
   [Luồng OTA chạy như Bước 7, publish trạng thái liên tục qua "ota/status"]
        │
   Thành công → esp_restart() → quay lại đầu sơ đồ (Rollback check trên app mới)
   Thất bại → ota_in_progress = false → tiếp tục chờ lệnh tiếp theo
```

---

## 13. Checklist Testing

| Kịch bản | Cách test |
|---|---|
| Safe update — mất điện giữa OTA | Rút nguồn ESP32 giữa lúc `WRITING_FLASH` → cấp lại nguồn → xác nhận vẫn boot app cũ |
| **Trigger MQTT — lệnh hợp lệ** | `mosquitto_pub -h localhost -t "ota/command" -m '{"version":"1.2.0","url":"https://192.168.1.100:8070/app_signed.bin"}'` → xác nhận thiết bị bắt đầu OTA, thấy log `OTA_START` trên `ota/status` |
| **Trigger MQTT — version không mới hơn** | Gửi lại đúng version hiện tại đang chạy → xác nhận thiết bị **bỏ qua**, không tải gì (kiểm tra bằng log `ESP_LOGI` "không mới hơn") |
| **Trigger MQTT — gửi trùng lặp khi đang OTA** | Gửi liên tiếp 2 lệnh ngay khi lệnh đầu vừa bắt đầu → xác nhận lệnh thứ 2 bị bỏ qua (cờ `ota_in_progress`) |
| **Trigger MQTT — OTA thất bại giữa chừng** | Ngắt WiFi tạm thời giữa lúc tải → xác nhận `ota_in_progress` được reset về `false`, thiết bị vẫn nhận được lệnh OTA tiếp theo bình thường (không bị kẹt) |
| App Rollback — app mới lỗi | Cố tình để `diagnostic()` trả `false` → xác nhận tự rollback, nhận được `APP_INVALID_ROLLING_BACK` qua MQTT |
| Secure OTA (chữ ký) | Thử OTA file `.bin` **chưa ký** hoặc ký sai khóa → xác nhận `SIGNATURE_INVALID` |
| HTTPS local | Dùng `curl -k https://<ip>:8070/app_signed.bin` xác nhận server phản hồi đúng trước khi test qua ESP32 |
| Giám sát MQTT | `mosquitto_sub -h localhost -t "ota/status" -v` — quan sát đầy đủ chuỗi trạng thái từ `OTA_START` → `REBOOTING` |
| Tuning Performance | So sánh thời gian OTA khi bật/tắt `bulk_flash_erase` |

---

## 14. Danh sách Example tham khảo tổng hợp

| Example | Dùng cho phần nào trong tài liệu này |
|---|---|
| [`system/ota/advanced_https_ota`](https://github.com/espressif/esp-idf/tree/master/examples/system/ota/advanced_https_ota) | Khung sườn chính — Event Handler, API advanced, Tuning Performance (Bước 6, 7, 8) |
| [`system/ota/native_ota_example`](https://github.com/espressif/esp-idf/tree/v6.1/examples/system/ota/native_ota_example) | Mẫu `diagnostic()` + App Rollback (Bước 10) |
| [`system/ota/otatool`](https://github.com/espressif/esp-idf/tree/v6.1/examples/system/ota/otatool) | Test nhanh logic chọn slot/rollback trước khi tích hợp OTA thật qua mạng |
| [`protocols/mqtt/tcp`](https://github.com/espressif/esp-idf/tree/master/examples/protocols/mqtt/tcp) | Mẫu kết nối MQTT cơ bản tới broker local (Bước 5) |
| [`protocols/mqtt/ssl`](https://github.com/espressif/esp-idf/tree/master/examples/protocols/mqtt/ssl) | Tham khảo nếu muốn bảo mật thêm kênh MQTT bằng TLS |
| [`security/signed_app_no_secure_boot`](https://github.com/espressif/esp-idf/tree/master/examples/security) (nếu có trong version) | Tham khảo cấu hình `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` |

---

## 15. Nguồn tham khảo chính thức

- [Over The Air Updates (OTA) — ESP-IDF Docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html)
- [ESP HTTPS OTA — API Reference & Events](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_https_ota.html)
- [Signed App Verification Without Hardware Secure Boot](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/security/secure-boot-v1.html#signed-app-verify)
- [Partition Tables](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html)
- [ESP-MQTT Component](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/mqtt.html)
- [`espsecure.py` — Signing Key Generation](https://docs.espressif.com/projects/esptool/en/latest/esp32/espsecure/index.html)
