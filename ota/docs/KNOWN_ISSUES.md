# Các vấn đề còn tồn đọng — review theo tiêu chuẩn công nghiệp

> Review luồng OTA hiện tại (`main/main.c` + `components/ota_manager/*`) dưới góc nhìn kỹ sư nhúng lâu năm, sau khi tính năng đã chạy được thành công trên board thật (xem `OTA_PLAN.md`). Đây là các vấn đề **còn tồn đọng, chưa sửa** — liệt kê để biết rủi ro đang chấp nhận, và làm checklist trước khi đưa lên production thật.

---

## 🔴 Nghiêm trọng — nên sửa trước khi đưa vào production

### 1. Kênh trigger OTA hoàn toàn không có xác thực

`broker.hivemq.com` là broker công cộng, ẩn danh — không cần đăng nhập để publish/subscribe bất kỳ topic nào. Bất kỳ ai trên Internet biết (hoặc đoán được) tên topic `esp32/vietnq/ota/cmd` đều publish được lệnh OTA với `url` tùy ý.

Chữ ký firmware (Secure OTA Without Secure Boot) chặn được payload giả mạo hoàn toàn — kẻ tấn công không thể tự tạo firmware giả và ép cài vào máy vì không có private key. Nhưng cơ chế hiện tại **không chặn được**:

- **Downgrade attack**: `is_version_newer()` trong `ota_manager.c` chỉ `strcmp()` xem version có **khác** version đang chạy hay không, không so sánh version nào **lớn hơn**. Kết hợp với việc đã chủ động bỏ Anti-Rollback (theo yêu cầu ban đầu), kẻ tấn công có thể ép thiết bị cài lại 1 bản firmware **cũ hơn** đã từng được ký hợp lệ nhưng có lỗ hổng đã biết.
- **DoS / hao mòn phần cứng**: spam lệnh OTA liên tục → tốn băng thông, và mỗi lần erase/ghi flash làm giảm tuổi thọ chip flash (số chu kỳ erase có giới hạn vật lý).
- **Lộ thông tin vận hành**: version, tiến trình OTA của thiết bị hiển thị công khai cho bất kỳ ai subscribe `esp32/vietnq/ota/status` / `esp32/vietnq/version`.

**Hướng khắc phục cho production:**
- Chuyển sang broker riêng có xác thực (username/password tối thiểu, tốt hơn là client certificate qua TLS).
- Cân nhắc ký số ngay trên payload lệnh OTA (không chỉ dựa vào transport) — vd HMAC hoặc chữ ký số trên JSON, verify phía device trước khi tin `url`/`version` trong lệnh.
- Nếu muốn giữ broker công cộng cho mục đích test, ít nhất nên đổi tên topic thành chuỗi khó đoán (không đủ để bảo mật thật, chỉ giảm rủi ro bị quét ngẫu nhiên).

### 2. `mqtt_start()` có thể bị gọi lại nhiều lần → leak MQTT client

```c
// main/main.c
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        mqtt_start();   // KHÔNG có guard kiểm tra đã có client chưa
    }
}
```

`IP_EVENT_STA_GOT_IP` không chỉ bắn 1 lần duy nhất trong đời thiết bị — nó bắn lại mỗi khi WiFi renew DHCP lease, hoặc sau **mọi lần** rớt sóng rồi reconnect lại (đây là hành vi chuẩn của ESP-IDF, không phải bug lạ). Mỗi lần `mqtt_start()` chạy lại, `esp_mqtt_client_init()` tạo ra **1 client MQTT hoàn toàn mới**; client cũ **không hề bị** `esp_mqtt_client_destroy()`, vẫn tiếp tục chạy ngầm (giữ kết nối TCP, task riêng, buffer riêng).

**Hậu quả:** thiết bị chạy càng lâu, WiFi càng hay chập chờn (rất bình thường ngoài thực tế) → càng tích lũy nhiều client MQTT chồng chéo → cạn heap dần, có thể crash hoặc treo sau vài ngày/vài tuần vận hành liên tục. Đây là loại bug **chỉ lộ ra sau thời gian chạy dài thật**, hoàn toàn không thấy được trong demo/test ngắn hạn vài phút.

**Hướng khắc phục:** thêm guard kiểm tra `s_mqtt_client == NULL` trước khi tạo mới; nếu đã có client, gọi `esp_mqtt_client_reconnect()` thay vì `esp_mqtt_client_init()` lại từ đầu.

### 3. WiFi bỏ cuộc vĩnh viễn sau 5 lần retry, không backoff

```c
#define WIFI_MAX_RETRY 5
```

Không có delay giữa các lần `esp_wifi_connect()` liên tiếp trong `wifi_event_handler()`. Một đợt sóng yếu thoáng qua vài giây (AP tạm reboot, nhiễu RF...) có thể ăn hết 5 lần thử trong chưa tới 1 giây → `ESP_LOGE("Failed to connect to wifi after %d retries, giving up")` rồi **dừng hẳn, không bao giờ tự thử lại nữa** — thiết bị cần rút nguồn/reset tay mới hồi phục được.

Trong khi đó MQTT (thư viện esp-mqtt) lại tự động reconnect **vô hạn** theo mặc định — 2 lớp có thiết kế retry không nhất quán, và vì WiFi là lớp thấp hơn nên MQTT dù retry giỏi tới đâu cũng vô ích nếu WiFi đã đầu hàng trước.

Với thiết bị IoT chạy không người trông coi (đúng use case của OTA), đây là lỗi cổ điển hay gặp khi review: mọi cơ chế reconnect ở tầng kết nối nền tảng (WiFi) đều phải retry **vô hạn có backoff** (cố định vài giây hoặc exponential), tuyệt đối không được có giới hạn cứng rồi im lặng bỏ cuộc.

**Hướng khắc phục:** bỏ `WIFI_MAX_RETRY` dạng đếm-rồi-dừng; thay bằng vòng lặp retry vô hạn với delay tăng dần (vd bắt đầu 1s, nhân đôi tới trần 30-60s), reset lại delay khi kết nối thành công.

---

## 🟡 Nên sửa — không chặn go-live nhưng để lại rủi ro đã biết

### 4. Không có device ID/MAC trong topic

Mọi lệnh OTA broadcast cho **tất cả thiết bị** đang subscribe cùng topic — không target được từng máy riêng lẻ nếu triển khai nhiều thiết bị cùng lúc. Không hỗ trợ canary rollout (thử nghiệm trên 1-2 máy trước khi rollout toàn bộ fleet).

**Hướng khắc phục:** đổi topic sang dạng có định danh riêng, vd `esp32/vietnq/<mac_hoặc_id>/ota/cmd`, lấy ID qua `esp_efuse_mac_get_default()`.

### 5. Cert HTTPS tự ký hết hạn sau 365 ngày

Cert self-signed tạo ở Bước 6 (`docs/OTA_PLAN.md`) có hạn 365 ngày. Khi hết hạn, thiết bị ngoài field **mất khả năng OTA vĩnh viễn qua đúng kênh OTA** — vì cần OTA để nhận cert mới, nhưng cert cũ đã hết hạn nên không OTA được (vòng lặp con gà - quả trứng). Chỉ còn cách reflash tay qua USB từng máy — không khả thi nếu thiết bị đã ở xa/khó tiếp cận.

**Hướng khắc phục cho production:** dùng cert từ CA công cộng (Let's Encrypt...) + `esp_crt_bundle_attach` (đã ghi chi tiết trong `OTA_PLAN.md` mục "Nếu triển khai production"), hoặc có quy trình chủ động xoay vòng cert trước khi hết hạn (thay vì để hết hạn rồi mới xử lý).

### 6. Chưa bật `ota_resumption`

`esp_https_ota_config_t` có sẵn field `ota_resumption` (ESP-IDF v5.5.5 trở lên) cho phép tiếp tục tải dở sau khi mất kết nối giữa chừng, thay vì tải lại từ đầu. Hiện chưa bật — mất mạng giữa chừng (WiFi/HTTPS chập chờn) phải tải lại toàn bộ ~1MB từ đầu, tốn băng thông và thời gian, đáng chú ý hơn với mạng kém ổn định ngoài thực tế so với môi trường LAN test.

---

## 🟢 Nice-to-have

### 7. Không lưu lịch sử OTA vào NVS

Toàn bộ trạng thái OTA hiện chỉ tồn tại dưới dạng log serial + publish MQTT tức thời — nếu thiết bị offline lâu và không ai theo dõi MQTT real-time đúng lúc sự cố xảy ra, mất hết thông tin để chẩn đoán sau này (lần OTA gần nhất thành công khi nào, thất bại vì lý do gì...). Nên cân nhắc ghi lại vài bản tin gần nhất vào NVS để đọc lại được khi cần debug từ xa.

---

## Ưu tiên đề xuất khi bắt tay sửa

1. **#2 và #3** trước — đều là bug thuần code, sửa độc lập, không cần quyết định kiến trúc gì thêm.
2. **#1** cần quyết định phương án xác thực MQTT (đổi broker/thêm auth) trước khi code.
3. **#4, #5, #6** có thể làm sau, tùy mức độ ưu tiên khi thực sự chuẩn bị deploy fleet thật.
4. **#7** làm khi rảnh, không gấp.
