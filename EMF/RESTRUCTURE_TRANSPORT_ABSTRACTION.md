# Restructure project EMF sang Transport Abstraction (WiFi/MQTT hoặc LoRaWAN qua RAK3172)

> Tài liệu này dành cho 1 phiên AI **có quyền truy cập source code thật của project EMF** (source không có trong môi trường viết tài liệu này). Mục tiêu: tách phần giao tiếp mạng (WiFi/MQTT và LoRaWAN qua RAK3172) hiện đang gọi trực tiếp rải rác trong code, thành 1 lớp trừu tượng chung (`transport_if_t`) — **chọn đúng 1 trong 2 giao thức lúc boot, dựa theo cấu hình lưu NVS, không phải DIP switch vật lý**.
>
> **Yêu cầu cứng, không thương lượng:** giữ nguyên toàn bộ driver/function EMF đã có (driver WiFi, driver RAK3172/UART AT-command, logic MQTT hiện tại...) — đây là công việc **bọc (wrap)** code cũ đằng sau interface mới, **KHÔNG phải viết lại từ đầu**. Đụng vào càng ít file cũ càng tốt.
>
> Toàn bộ kiến trúc tham chiếu trong tài liệu này đã được **implement thật, build sạch** tại project song song `/home/vietnq/esp/esp32c5_rak3172` (cùng target ESP32-C5 + RAK3172) — mọi trích dẫn code trong tài liệu này đều lấy nguyên văn từ đó. Đọc project đó song song với tài liệu này để đối chiếu, đừng chỉ đọc mô tả.

---

## 0. BẮT BUỘC làm trước tiên: audit code EMF thật — không giả định gì cả

Tài liệu này viết ra mà **không có source code EMF trong tay**, nên không thể biết trước cấu trúc thật của EMF (có thể khác đáng kể so với các ví dụ dưới đây). Trước khi sửa bất kỳ dòng code nào, tự trả lời và ghi lại (comment hoặc note riêng) toàn bộ các câu hỏi sau bằng cách đọc/`grep` source EMF thật:

1. **Toàn bộ nơi gọi thẳng API WiFi/MQTT** — `grep -rn "esp_wifi_\|esp_mqtt_client_"` trên toàn bộ source. Liệt kê: file nào, hàm nào, gọi lúc nào (trong `app_main()` trực tiếp, hay trong 1 module riêng đã có sẵn?).
2. **Toàn bộ nơi gọi thẳng driver RAK3172/LoRa** — tìm driver UART AT-command hiện có (tên hàm, tên file có thể khác hoàn toàn so với ví dụ `rak3172_*` dùng trong tài liệu này). Ghi lại API surface hiện tại của driver đó (init, gửi lệnh, nhận downlink...).
3. **`app_main()` hiện tại** — luồng boot đầy đủ, thứ tự khởi tạo, có đang chạy đồng thời cả WiFi và LoRa không hay đã có logic chọn 1 trong 2 (dù thô sơ)?
4. **Định dạng bản tin lên/xuống hiện có** — JSON gì đang gửi qua MQTT? Payload LoRa đang mã hoá kiểu gì (JSON qua LoRa thường KHÔNG hợp lý vì giới hạn airtime — nếu EMF đang làm vậy, ghi chú lại để cân nhắc riêng, không tự ý đổi định dạng khi chỉ đang làm transport abstraction)?
5. **Cơ chế cấu hình hiện có** — EMF đã có NVS config struct nào chưa? Có CLI console/cách nào để người dùng nhập WiFi SSID, MQTT broker, LoRa keys không, hay đang hardcode?
6. **Kconfig hiện có cho RAK3172** (UART number, TX/RX GPIO, band index...) — lấy đúng giá trị EMF đã cấu hình, **không đoán lại theo ví dụ trong tài liệu này**.

Chỉ sau khi có đầy đủ danh sách trên mới tiếp tục đọc các mục dưới.

---

## 1. Kiến trúc tham chiếu: `transport_if_t`

Toàn bộ giao tiếp mạng được trừu tượng hoá thành 1 struct chứa con trỏ hàm (không có class/OOP trong C) — xem nguyên văn `/home/vietnq/esp/esp32c5_rak3172/components/transport/include/transport.h`:

```c
typedef struct {
    /* Bat dau ket noi (KHONG block) - vd WiFi connect roi doi MQTT connect,
     * hoac rak3172_init()+configure_identity()+join(). */
    esp_err_t (*start)(const app_config_t *cfg);

    /* Gui 1 ban tin ung dung len server. MOI transport TU QUYET DINH cach ma
     * hoa/dong goi/chon topic-hoac-port phu hop nhat cho tung app_msg_type_t. */
    esp_err_t (*publish)(const app_message_t *msg);

    /* true khi transport da san sang gui du lieu. */
    bool (*is_ready)(void);
} transport_if_t;
```

`app_message_t` (chiều lên server) và `app_command_t` (chiều xuống thiết bị) là **tagged union** — mỗi loại bản tin/lệnh tự có nhánh dữ liệu riêng, không ép mọi transport dùng chung 1 định dạng byte:

```c
typedef enum {
    APP_MSG_SENSOR,
    APP_MSG_KEEPALIVE,
    /* them loai ban tin moi: them 1 dong enum + 1 nhanh union tuong ung */
} app_msg_type_t;

typedef struct {
    app_msg_type_t type;
    union {
        float sensor_value;
        /* ... */
    } data;
} app_message_t;
```

**Vì sao truyền "dữ liệu thô" (union) thay vì 1 buffer đã encode sẵn**: mỗi transport tự quyết định cách mã hoá tối ưu riêng cho nó — MQTT dùng JSON text (dễ đọc, không giới hạn băng thông đáng kể), LoRaWAN dùng fixed-point vài byte (tiết kiệm airtime, quan trọng vì duty-cycle). Ép dùng chung 1 định dạng sẽ làm mất tối ưu của 1 trong 2 bên.

**2 callback, 2 ngữ nghĩa khác nhau** — cả 2 đăng ký TRƯỚC khi gọi `transport->start()`:

```c
/* Chi bao DUNG 1 LAN trong ca vong doi thiet bi (MQTT CONNECTED lan dau,
 * hoac LoRaWAN JOINED lan dau) - dam bao boi transport.c, KHONG phai tung
 * transport implementation tu lo. */
typedef void (*transport_ready_cb_t)(void);
void transport_set_ready_callback(transport_ready_cb_t cb);

/* Bao MOI LAN co lenh/ban tin tu server xuong - KHONG gioi han "1 lan". */
typedef void (*transport_downlink_cb_t)(const app_command_t *cmd);
void transport_set_downlink_callback(transport_downlink_cb_t cb);
```

Chọn đúng 1 implementation lúc boot, không hot-switch:

```c
const transport_if_t *transport_get(app_mode_t mode);
```

**Quan trọng — ai quyết định "bao lâu publish 1 lần"?** KHÔNG phải transport. `transport->publish()` gửi đúng 1 lần, ngay lập tức, khi được gọi — hoàn toàn không biết gì về chu kỳ/lịch trình. Tầng ứng dụng (`main.c` trong project tham chiếu) tự sở hữu lịch gửi và tự vòng lặp gọi lại. Đây là quyết định kiến trúc **quan trọng nhất cần giữ nguyên khi áp dụng vào EMF** — tránh nhét logic "cứ N giây gửi 1 lần" vào bên trong transport.

---

## 2. Nguyên tắc bọc code cũ — KHÔNG viết lại driver

Đây là phần trả lời trực tiếp yêu cầu "giữ nguyên driver/function đã có, động vào ít nhất có thể".

Tạo 2 file mới, `transport_wifi_mqtt.c` và `transport_lorawan.c` (tên có thể đặt khác tuỳ convention EMF), mỗi file:
- Implement đúng 3 hàm của `transport_if_t` (`start`/`publish`/`is_ready`).
- **Bên trong**, chỉ **gọi lại** các hàm driver/API đã tồn tại trong EMF (đã liệt kê ở Mục 0) — ví dụ nếu EMF đã có sẵn `wifi_connect()`/`mqtt_publish_raw()` hay tương đương, `transport_wifi_mqtt.c`'s `start()` chỉ đơn giản gọi `wifi_connect()` rồi đăng ký event handler để biết lúc nào gọi `transport_notify_ready()`.
- **KHÔNG sửa nội dung logic** của driver gốc (file UART AT-command của RAK3172, module WiFi/MQTT hiện có) — nếu driver gốc thiếu 1 hàm cần thiết (vd chưa có callback báo "đã joined"), **thêm hàm mới** vào driver đó thay vì viết lại hàm cũ, và ghi rõ lý do trong commit/PR.

Xem ví dụ thật cách `esp32c5_rak3172` làm điều này với `wifi_manager`/`rak3172` (2 component driver độc lập, không biết gì về `transport_if_t`) — `transport_wifi_mqtt.c`/`transport_lorawan.c` chỉ là lớp điều phối MỎNG bên trên, gọi API công khai của 2 component đó, không đụng vào bên trong chúng:
- `/home/vietnq/esp/esp32c5_rak3172/components/wifi_manager/` — driver WiFi, không biết gì về MQTT hay transport abstraction.
- `/home/vietnq/esp/esp32c5_rak3172/components/rak3172/` — driver UART AT-command cho RAK3172, không biết gì về LoRaWAN app logic hay transport abstraction.
- `/home/vietnq/esp/esp32c5_rak3172/components/transport/transport_wifi_mqtt.c` + `transport_lorawan.c` — lớp điều phối, gọi 2 driver trên qua API công khai của chúng.

Nếu EMF hiện tại đang gọi WiFi/MQTT/RAK3172 **trực tiếp trong `app_main()`** (không qua module riêng nào) — vẫn áp dụng được nguyên tắc trên: tạo `transport_wifi_mqtt.c`/`transport_lorawan.c` mới, **di chuyển nguyên văn** các đoạn code hiện có từ `app_main()` vào đúng hàm `start()`/`publish()` tương ứng (copy-paste + đổi chữ ký hàm, không viết lại logic bên trong), rồi xoá code cũ khỏi `app_main()`.

---

## 3. Chọn mode qua NVS — không DIP switch, không hot-switch

Thêm 1 field `app_mode_t mode` (enum `APP_MODE_WIFI_MQTT`/`APP_MODE_LORAWAN`) vào struct config hiện có của EMF (nếu EMF chưa có struct config lưu NVS nào, tạo 1 component `app_config` mới tối giản, xem `/home/vietnq/esp/esp32c5_rak3172/components/app_config/` làm mẫu — struct 1 blob, `magic`/`version`/`crc32` để tự phát hiện dữ liệu hỏng/lệch version).

Nguyên tắc:
- Chọn **đúng 1 lần lúc boot** — `app_main()` đọc `cfg.mode`, gọi `transport_get(cfg.mode)`, dùng đúng 1 implementation cho tới khi reset.
- Đổi mode (qua CLI, hoặc cơ chế cấu hình EMF đã có) **chỉ lưu vào NVS, cần reboot mới áp dụng** — không bao giờ chạy song song 2 transport, không hot-switch giữa chừng 1 phiên chạy.
- Nếu EMF hiện tại **luôn chạy cả WiFi lẫn LoRa cùng lúc** (không phải chọn 1-trong-2) — đây là khác biệt kiến trúc quan trọng so với project tham chiếu, cần xác nhận lại với người yêu cầu trước khi ép về mô hình "chọn 1", vì có thể EMF cố ý cần cả 2 đồng thời (tài liệu này giả định mô hình "chọn 1-trong-2" theo đúng yêu cầu ban đầu, nhưng đây là điểm PHẢI xác nhận lại ở Mục 0, không suy đoán).

---

## 4. Sửa `app_main()` — điểm nối tối thiểu

Sau khi có `transport_if_t` + 2 implementation + config mode, `app_main()` chỉ cần:

```c
s_transport = transport_get(cfg.mode);
transport_set_ready_callback(on_transport_ready);      // dang ky TRUOC start()
transport_set_downlink_callback(on_downlink_command);  // dang ky TRUOC start()
s_transport->start(&cfg);
```

Lịch gửi (bao lâu publish 1 lần) và logic nghiệp vụ khi nhận downlink **vẫn giữ nguyên tại `app_main()`/tầng ứng dụng** — xem Mục 1 về việc transport không được biết gì về chu kỳ. Nếu EMF hiện có logic publish định kỳ dạng `while(1) { ... vTaskDelay(...); }`, giữ nguyên cấu trúc đó, chỉ đổi lời gọi publish trực tiếp cũ thành `s_transport->publish(&msg)`.

---

## 5. Verification

1. `idf.py build` sạch, 0 lỗi/warning.
2. `grep -rn "esp_wifi_\|esp_mqtt_client_\|<ten_ham_rak3172_cu>"` trên toàn bộ source **ngoài** các file `transport_*.c` mới — phải KHÔNG còn kết quả nào (nghĩa là mọi lời gọi trực tiếp đã được chuyển hết vào lớp transport).
3. `git diff`/so sánh các file driver gốc (WiFi, MQTT, RAK3172) — phải KHÔNG có thay đổi logic nào ngoài việc thêm hàm mới nếu thực sự cần thiết (đã nêu ở Mục 2). Nếu buộc phải sửa 1 file driver gốc, ghi rõ lý do cụ thể trong commit message.
4. Test cả 2 mode qua cấu hình thật của EMF (`mode_set wifi_mqtt`/`mode_set lorawan` hoặc tương đương) — xác nhận publish/downlink vẫn hoạt động đúng như trước khi restructure, không có hồi quy về hành vi nghiệp vụ.

## Cảnh báo xuyên suốt

- **Không đoán giá trị phần cứng cụ thể** (UART number, GPIO, band index, timeout...) nếu EMF chưa cấu hình sẵn — giữ nguyên giá trị EMF đang dùng thật, hỏi lại người yêu cầu nếu thiếu.
- **Không tự ý đổi định dạng bản tin hiện có** (JSON MQTT, payload LoRa) khi chỉ đang làm transport abstraction — đây là 2 việc tách biệt, đổi định dạng bản tin là quyết định nghiệp vụ riêng, ngoài phạm vi tài liệu này.
- Sau khi xong tài liệu này, xem tiếp `/home/vietnq/esp/EMF/RETROFIT_OTA_ESP32C5.md` để thêm tính năng OTA — tài liệu đó **giả định transport abstraction đã áp dụng xong**, OTA sẽ chỉ được wire vào implementation WiFi/MQTT.
