# Image Transfer Profiler

Công cụ đo thời gian từng phiên truyền ảnh qua LoRaWAN: từ lúc Backend ra lệnh `Image` đến lúc tag nhận đủ chunk. Mỗi phiên cho ra một file JSON gồm thời gian từng chặng, số lần retry và những gì bị mất.

Bối cảnh luồng truyền ảnh xem ở [../docs/IMAGE_FLOW.md](../docs/IMAGE_FLOW.md).

```
transfer_profiler/
├── image_transfer_profiler.py   ← công cụ
├── profiler.ini                 ← cấu hình
├── requirements.txt             ← paho-mqtt, pyserial
└── README.md                    ← tài liệu này
```

---

## 1. Cách hoạt động

Công cụ chỉ **quan sát**, không gửi gì vào hệ thống. Nó nghe từ hai nguồn:

| Nguồn | Nghe gì | Cho biết |
|---|---|---|
| Broker MQTT (dùng chung cho cả hệ thống) | `<dev_id>/Command` | Backend ra lệnh Image (bắt đầu phiên) |
| | `downlink/<devaddr>` | Bridge đẩy lệnh và từng chunk xuống gateway |
| | `uplink/...` | Gateway báo thiết bị gửi lên: init request, xin chunk, Handshake. Có trường `timestamp` do gateway thêm |
| Cổng COM của ESP32 | 2 dòng log theo regex | Lúc thiết bị **xin** chunk và lúc **nhận** chunk |

ESP32 không có đồng hồ thực, nên mỗi dòng log được gắn giờ của máy chạy tool lúc dòng đó đến. Độ trễ của UART/USB thường chỉ vài ms.

### Một chunk đi qua những chặng nào

```
 ESP32 log        Gateway           Tool thấy ở broker       Bridge             ESP32 log
 "Request chunk"  nhận uplink       uplink/...               publish            "Got chunk"
      │               │                  │                   downlink/...          │
      ├─ dev_to_gw ──►│── gw_to_broker ─►│──── bridge_ms ───────►│──────── down_ms ──┤
      └──────────────────────────── total_ms (một lần thử) ─────────────────────────┘
```

- Hai chặng `dev_to_gw` và `gw_to_broker` cần **đồng hồ gateway khớp với máy chạy tool**.
- `bridge_ms` đo giữa hai bản tin cùng đi qua broker, nên không phụ thuộc đồng hồ gateway.
- `down_ms` gồm broker → gateway → hàng đợi Class A → cửa sổ RX → sóng vô tuyến → module → log ESP32.

### Vòng đời một phiên

- **Bắt đầu** khi Backend gửi `Command: "Image"` tới `<dev_id>/Command` (`trigger: backend-command`). Nếu tool khởi động muộn thì phiên bắt đầu ở lệnh downlink Image (`downlink-command`), hoặc ở `ImageRequest` đầu tiên của thiết bị (`device-init-request`), hoặc giữa chừng ở một chunk request (`mid-transfer`). Các phiên bắt đầu muộn thiếu một vài mốc đầu.
- **Kết thúc** khi một trong các điều kiện sau thoả:

| `result` | Điều kiện |
|---|---|
| `complete` | Thiết bị đã nhận đủ `TOTAL_CHUNKS` chunk (hoặc Handshake báo `Complete`), sau `grace` giây |
| `device-failed` | Handshake báo `Failed` |
| `timeout` | Không có sự kiện nào trong `idle_timeout` giây |
| `superseded` | Một phiên mới bắt đầu khi phiên cũ chưa xong |
| `interrupted` | Bạn bấm Ctrl+C |

Khi không dùng serial (`serial_port` trống), "đã nhận" được thay bằng "Bridge đã gửi downlink", nên số liệu phía thiết bị không có.

### Lọc thiết bị

Bản tin trên đường đi mang định danh khác nhau, nên cần điền cả ba:

| Khoá | Lọc |
|---|---|
| `dev_id` | topic `<dev_id>/Command` và `DeviceID` trong downlink lệnh Image |
| `deveui` | uplink từ gateway |
| `devaddr` | uplink và `downlink/<devaddr>`; chunk downlink chỉ có `devaddr` |

Để `devaddr` trống thì tool tự học (nên dùng với OTAA, vì `devaddr` đổi sau mỗi lần join lại).

Một tiến trình chỉ theo dõi một thiết bị. Muốn đo nhiều tag thì chạy nhiều tiến trình, mỗi tiến trình một `profiler.ini` (xem mục 3).

---

## 2. Chuẩn bị trước khi chạy

1. **Python 3.10 trở lên**, rồi cài thư viện:
   ```bash
   cd transfer_profiler
   pip install -r requirements.txt
   ```
2. **Giữ nguyên cấu trúc thư mục**. Tool import module `protocol` của Bridge qua đường dẫn `../lorawan_bridge_services/src`. Nếu copy sang máy khác, phải copy kèm thư mục đó.
3. **Broker MQTT** mà cả Gateway, Bridge và Backend cùng dùng phải truy cập được từ máy chạy tool. Nếu broker không nằm trên máy này, đổi `broker` trong `profiler.ini`.
4. **Gateway phải thêm trường timestamp** vào bản tin MQTT. Xác nhận tên trường và đơn vị (giây, ms, µs, ns hoặc chuỗi ISO-8601 đều nhận được). Tên mặc định là `timestamp`, đổi bằng `ts_field`. Trường lồng nhau viết dạng `rxInfo.time`. Chuỗi ISO không có múi giờ được coi là UTC.
5. **Đồng bộ đồng hồ**: máy chạy tool và gateway phải đồng bộ NTP. Lệch vài trăm ms là làm sai hai chặng `dev_to_gw`, `gw_to_broker`. Có thể bù bằng `gw_offset_ms`. Chạy trên nhiều máy mà muốn so sánh chéo thì các máy cũng phải đồng bộ với nhau.
6. **Cáp serial của ESP32** cắm vào máy chạy tool, và biết tên cổng (`COM5` trên Windows, `/dev/ttyUSB0` trên Linux). Tool tắt DTR/RTS khi mở cổng để không làm reset ESP32. Trên **WSL2** cổng COM không tự xuất hiện: attach thiết bị bằng `usbipd-win`, hoặc chạy tool bằng Python của Windows.
   - **Đóng `idf.py monitor` (và mọi chương trình đang giữ cổng) trước khi chạy tool.** Một cổng COM chỉ được mở bởi một chương trình, nên tool không đọc chung cổng với monitor được. Tool thay monitor: đặt `echo = true` để in mọi dòng log ra console (đã bỏ mã màu ANSI của ESP-IDF), và `raw_log` để lưu toàn bộ log kèm giờ. Xem mục 3 "Dùng thay `idf.py monitor`".
7. **Hai chuỗi log của firmware** (regex):
   - `req_pattern`: dòng in ra khi thiết bị xin chunk.
   - `recv_pattern`: dòng in ra khi thiết bị nhận chunk.
   - Dùng nhóm `(?P<idx>\d+)` để lấy số chunk. Ví dụ log `Request chunk 12` thì regex là `Request chunk (?P<idx>\d+)`. Nếu dòng log không chứa số chunk, tool tự suy ra (chunk kế tiếp sau những chunk đã nhận), nhưng kém chắc chắn hơn. Sự kiện suy ra có `idx_inferred: true`.
8. **Biết `dev_id`, `deveui`, `devaddr`** của tag.

### Cấu hình ([profiler.ini](profiler.ini))

| Khoá | Ý nghĩa | Mặc định trong file |
|---|---|---|
| `broker`, `port` | Địa chỉ broker | `localhost`, `1883` |
| `client_id` | ID MQTT; để trống thì tool sinh ID riêng mỗi lần chạy (cần khi nhiều máy cùng chạy) | trống |
| `username`, `password` | Tài khoản broker nếu có | trống |
| `dev_id`, `deveui`, `devaddr` | Định danh thiết bị | `T3CDC75810B64`, `AC1F09FFFE1E49B2`, trống |
| `ts_field` | Tên trường timestamp của gateway | `timestamp` |
| `gw_offset_ms` | Cộng vào timestamp gateway để khớp đồng hồ máy chạy tool | `0` |
| `serial_port`, `baud` | Cổng và tốc độ log ESP32 | `COM5`, `115200` |
| `req_pattern`, `recv_pattern` | Hai regex log | `Request chunk (?P<idx>\d+)`, `Got chunk (?P<idx>\d+)` |
| `echo` | `true` thì in mọi dòng serial ra console (dùng thay monitor) | `true` |
| `raw_log` | File ghi toàn bộ log serial kèm giờ PC, ghi nối tiếp; trống thì không ghi | `serial_raw.log` |
| `out_dir` | Thư mục ghi báo cáo, **tính theo thư mục hiện tại lúc chạy** | `transfer_logs` |
| `idle_timeout` | Giây không có sự kiện thì đóng phiên | `120` |
| `grace` | Giây giữ phiên đã xong để nhận sự kiện muộn | `5` |

Giá trị để trống thì bị bỏ qua. Ghi sai tên khoá thì tool báo lỗi ngay.

---

## 3. Cách chạy

```bash
cd transfer_profiler
python image_transfer_profiler.py                       # đọc profiler.ini
python image_transfer_profiler.py --config tag2.ini     # dùng file khác
python image_transfer_profiler.py --broker 10.0.0.5     # cờ dòng lệnh ghi đè file
python image_transfer_profiler.py --help                # xem mọi tuỳ chọn
```

Khi khởi động, tool in:
```
MQTT connected to 127.0.0.1:1883 as profiler-T3CDC75810B64-3fb3cc (rc=Success)
Serial opened: COM5 @ 115200
Waiting for image transfers... (Ctrl+C to stop)
```

Sau đó **tool chờ**. Để có một phiên, bạn gửi lệnh Image cho tag, bằng `APItest.py` (chọn `Image`, điền host/port/URL) hoặc `mosquitto_pub` tới `<dev_id>/Command`. Hết phiên, tool in tóm tắt và ghi file JSON. Bấm Ctrl+C để thoát.

Cổng serial sai hoặc broker không kết nối được thì tool thoát ngay với thông báo lỗi.

### Dùng thay `idf.py monitor`

Khi đo, bạn không chạy `idf.py monitor` song song. Tool tự đọc cổng COM bằng `pyserial` và làm thay phần xem log. **Tool không gọi `idf.py` và không cần cài ESP-IDF**, nên không cần mở terminal ESP-IDF; chỉ cần Python, `pip install -r requirements.txt` và tên cổng COM (xem trong Device Manager hoặc thanh trạng thái VS Code ESP-IDF).

Việc build/nạp firmware vẫn dùng `idf.py flash` như bình thường, nhưng phải làm **trước** khi chạy tool và đóng lệnh đó (kể cả monitor đi kèm `flash monitor`) để cổng được giải phóng.

| Việc monitor làm | Tool |
|---|---|
| Hiện log theo thời gian thực | `echo = true`: mỗi dòng in dạng `HH:MM:SS.mmm \| <log>` |
| Lưu log | `raw_log = serial_raw.log` |
| Reset chip khi mở cổng | Không. Tool tắt DTR/RTS, nên chip chạy tiếp. Muốn xem log boot thì bấm nút reset trên board |
| Giải mã backtrace, phím tắt Ctrl+T, màu sắc | **Không có.** Cần thì tắt tool và mở lại `idf.py monitor` |

Trên Windows với ESP-IDF cho VS Code, chạy tool bằng Python của Windows (cùng máy có cổng COM):
```powershell
cd transfer_profiler
py -3 -m pip install -r requirements.txt
py -3 image_transfer_profiler.py
```
Tool import module `protocol` từ `..\lorawan_bridge_services\src`, nên thư mục repo phải truy cập được từ Windows (copy cả repo, hoặc mở qua `\\wsl$\...`).

Thứ tự khi test: đóng monitor → chạy tool → bấm reset board nếu cần → gửi lệnh Image.

### Chạy với nhiều thiết bị

- Mỗi thiết bị một tiến trình, mỗi tiến trình một file config (`dev_id`, `deveui`, `devaddr`, `serial_port` riêng) và một `out_dir` riêng.
- Có thể chạy trên nhiều máy. Client ID mặc định đã khác nhau mỗi lần chạy, nên không đá nhau khỏi broker.
- Khi nhiều tag truyền cùng lúc, số liệu khác với đo từng tag. Xem mục 5.

---

## 4. Đọc kết quả

### Console

```
[session 1] COMPLETE  chunks ok 6/6  total 24400 ms  -> transfer_logs/session_20261011_001924_T3CDC75810B64_1.json
    stages: backend->bridge 146 ms | cmd->device init 1202 ms | init->1st chunk req 2002 ms | chunk phase 21050 ms
    chunk total  mean 1317.9 p50 1317.5 p95 1324.3 max 1324.3 ms
    dev->gw      mean 312.3 p50 311.6 p95 315.8 max 315.8 ms
    bridge       mean 61.8 p50 60.9 p95 63.3 max 63.3 ms
    down         mean 902.2 p50 900.7 p95 905.6 max 905.6 ms
    retries 3 | lost UL 1 | no bridge reply 0 | lost DL 2 | missing []
    ! chunk 2: 1 uplink request(s) not seen at gateway
    ! chunk 3: 1 downlink(s) sent but not received (RX window missed / RF loss)
```

Dòng `!` là các bất thường. Dòng cuối của khối `!` cho biết còn bao nhiêu mục khác nằm trong file.

### File JSON

Tên file: `session_<ngày>_<giờ bắt đầu>_<dev_id>_<số thứ tự>.json`. Các mục theo thứ tự nên đọc:

| Mục | Nội dung |
|---|---|
| `session` | `id`, `trigger`, `dev_id`, giờ bắt đầu/kết thúc, `result` |
| `summary` | Số liệu tổng hợp (mục dưới) |
| `anomalies` | Danh sách bất thường bằng lời |
| `chunks` | Bảng từng chunk, một dòng mỗi chunk |
| `events` | Toàn bộ bản tin và dòng log theo thứ tự thời gian (`rel_ms` tính từ lúc bắt đầu phiên) |
| `config` | Cấu hình đã dùng để chạy |

#### `summary.stages_ms`: các chặng của cả phiên

| Khoá | Đo từ → đến | Nên hiểu |
|---|---|---|
| `backend_cmd_to_bridge_downlink` | Backend gửi lệnh → Bridge đẩy downlink | Thời gian Bridge xử lý lệnh |
| `command_delivery_to_device_init_request` | Downlink lệnh → uplink `ImageRequest` đầu | Lệnh nằm chờ trong hàng đợi Class A tới uplink kế tiếp, cộng thời gian thiết bị phản ứng |
| `init_request_to_first_chunk_request` | `ImageRequest` đầu → chunk request đầu | **Cận trên** thời gian Bridge tải HTTP và nén zlib, cộng độ trễ của thiết bị. Không đo riêng được |
| `chunk_phase` | Chunk request đầu → chunk cuối được nhận | Pha truyền chunk, gồm cả thời gian retry |
| `total` | Bắt đầu phiên → kết thúc | Tổng |

#### `chunks`: các cột của một chunk

| Cột | Ý nghĩa |
|---|---|
| `device_requests` | Số lần dòng log "xin chunk" xuất hiện |
| `gateway_uplinks` | Số uplink xin chunk thấy ở broker |
| `bridge_downlinks` | Số downlink chunk Bridge đã gửi |
| `device_received` | Số lần dòng log "nhận chunk" xuất hiện |
| `retries` | Số lần xin lại (= `device_requests` − 1) |
| `lost_uplink` | `device_requests` − `gateway_uplinks`: thiết bị xin nhưng gateway không thấy |
| `no_bridge_reply` | `gateway_uplinks` − `bridge_downlinks`: Bridge nhận uplink nhưng không trả lời |
| `lost_downlink` | `bridge_downlinks` − `device_received`: Bridge gửi nhưng thiết bị không nhận |
| `duplicate_received` | Nhận trùng |
| `dev_to_gw_ms` | Log "xin chunk" → timestamp gateway của uplink |
| `gw_to_broker_ms` | Timestamp gateway → lúc tool thấy uplink ở broker |
| `bridge_ms` | Uplink tới broker → Bridge publish downlink |
| `down_ms` | Downlink publish → log "nhận chunk" |
| `total_ms` | Log xin → log nhận, **chỉ lần thử thành công cuối cùng** |
| `total_with_retries_ms` | Từ lần xin **đầu tiên** → lần nhận đầu |
| `rx1_risk_ms` | Chỉ có khi phản hồi quá 800 ms (xem mục 5) |
| `status` | `ok`, `ok_after_retry`, `missing` (không nhận được), `never_requested` |

Các cột thời gian của chunk dùng lần thử cuối cùng thành công, nên `dev_to_gw + gw_to_broker + bridge + down = total` (cộng được). Chunk `missing` thì không có các cột thời gian.

Số lần mất bản tin là **phép trừ số lượng giữa các chặng**, không truy vết từng gói. Nó đúng cho các tình huống thường gặp, nhưng không thay thế được log gateway khi cần chắc chắn.

#### `summary.retries`

`chunk_retries_total`, `chunks_with_retry` (danh sách chunk phải xin lại), `lost_uplink_total`, `no_bridge_reply_total`, `lost_downlink_total`, `duplicate_received_total`, và hai số về cả phiên: `init_request_repeats` (init request lặp lại), `command_downlink_repeats` (Bridge gửi lại lệnh).

#### Khác

- `per_chunk_stats_ms`: min/mean/p50/p95/max cho mỗi chặng. **Nhìn `p95` và `max`**, vì chuyện hay xảy ra ở đuôi phân bố.
- `goodput_bytes_per_s`: byte dữ liệu nén chia cho thời gian pha chunk. Đây là tốc độ thực dùng được.
- `gateway_clock_skew_hint_ms`: trung vị `gw_to_broker_ms`. Nên là vài chục ms. Nếu lệch hơn 1 giây hoặc có chặng âm, đồng hồ không khớp và `anomalies` có cảnh báo.

---

## 5. Dùng kết quả để chẩn đoán

### Quy trình đọc

1. Xem `result` và `chunks_missing`. Phiên `timeout` mà có chunk thiếu là lỗi nặng nhất.
2. Đọc `anomalies` để biết chunk nào có vấn đề.
3. So `retries` với số chunk: tỉ lệ retry cao là dấu hiệu đường truyền hoặc Bridge có vấn đề.
4. Nhìn `p95`/`max` của từng chặng trong `per_chunk_stats_ms` để biết chặng nào chậm.
5. Với chunk cụ thể, mở `events` và xem trình tự để hiểu chuyện gì đã xảy ra.

### Bảng chẩn đoán

| Dấu hiệu | Nguyên nhân có thể | Cần kiểm tra |
|---|---|---|
| `lost_uplink` > 0 | Uplink không tới gateway: sóng yếu, va chạm với thiết bị khác, SF cao | RSSI/SNR ở gateway, số thiết bị đang truyền, gateway có nhận được không |
| `no_bridge_reply` > 0 | Bridge nhận uplink nhưng không đẩy chunk. Trong code: chưa có transfer (`No image transfer found`, thường do thiết bị xin chunk trước khi Bridge tải xong), `chunk_index` ≥ tổng số chunk, hoặc transfer đã hết hạn 20 phút | Log của Bridge. Xem `init_request_to_first_chunk_request`: Bridge tải ảnh lâu hơn thiết bị chờ |
| `lost_downlink` > 0 | Bridge đã gửi nhưng thiết bị không nhận: trả lời quá chậm nên lỡ cửa sổ RX, hoặc hàng đợi gateway, hoặc lỗi vô tuyến | Cột `rx1_risk_ms`; `bridge_ms` của chunk đó; log gateway |
| `bridge_ms` lớn | Bridge bận. Điểm nghẽn có sẵn: `requests.get` đồng bộ chặn event loop tới 30 giây khi Bridge đang tải ảnh cho tag khác | Có tag nào khác vừa bắt đầu phiên không; Redis |
| `rx1_risk_ms` có giá trị (> 800 ms) | Thời gian gateway → Bridge → trả lời vượt khoảng RX1 (thường 1 giây trong LoRaWAN mặc định, mạng của bạn có thể cấu hình khác) | Giảm tải Bridge; kiểm tra độ trễ mạng tới broker |
| `down_ms` ≈ 1000 ms | Thường là RX1 | Bình thường |
| `down_ms` ≈ 2000 ms | Thường là RX2 (RX1 bị lỡ) | Kiểm tra `bridge_ms` |
| `down_ms` lớn hơn nhiều (vài giây trở lên) | Downlink nằm chờ ở gateway tới uplink kế tiếp (Class A) hoặc đã có retry | `retries`; thiết bị có gửi uplink đều đặn không |
| `dev_to_gw_ms` lớn và dao động | Thời gian phát sóng (airtime) lớn do SF cao, hoặc log ESP32 ra trễ so với lúc thật sự phát | Data rate của thiết bị; vị trí log trong firmware |
| `dev_to_gw_ms` âm, hoặc `gw_to_broker_ms` âm / lớn bất thường | Đồng hồ gateway lệch so với máy chạy tool | Đồng bộ NTP; chỉnh `gw_offset_ms`; xem `gateway_clock_skew_hint_ms` |
| `command_delivery_to_device_init_request` rất lớn | Lệnh chờ lâu ở hàng đợi Class A vì thiết bị ít gửi uplink | Chu kỳ heartbeat của tag |
| `init_request_repeats` > 0 | Thiết bị gửi lại init request: không thấy phản hồi như mong đợi | Bridge tải ảnh thất bại (HTTP lỗi, sai kích thước ảnh), xem log Bridge |
| `command_downlink_repeats` > 0 | Lệnh Image bị gửi nhiều lần | Backend có gửi lặp không |
| `duplicate_received` > 0 | Thiết bị nhận trùng một chunk (thường do downlink đến muộn sau khi đã xin lại) | Đi cùng `lost_downlink`/`rx1_risk_ms` |
| `total_with_retries_ms` ≫ `total_ms` | Phần lớn thời gian mất ở retry chứ không phải ở độ trễ mỗi lần | Tìm nguyên nhân retry ở các dòng trên |
| `goodput_bytes_per_s` thấp | Tổng hợp của các nguyên nhân trên, cộng chunk nhỏ phải đi từng vòng uplink-downlink | Nhìn `p95` các chặng |

### Chú ý khi nhiều thiết bị truyền cùng lúc

Khi các tag truyền chồng lên nhau, `bridge_ms` có thể tăng cho **tất cả** thiết bị cùng thời điểm (Bridge đang tải ảnh của một tag), và số lần `lost_uplink` có thể tăng vì va chạm. Để tách ảnh hưởng, đo từng tag riêng trước rồi mới đo đồng thời, và so sánh `p95`.

### Xem trình tự một chunk

Tìm trong `events` theo `idx`. Một chunk bình thường có thứ tự:
```
serial_chunk_request  →  uplink_chunk_request  →  downlink_chunk  →  serial_chunk_received
```
Thiếu một bước nghĩa là mất bản tin ở chặng đó. Ví dụ có `serial_chunk_request` nhưng không có `uplink_chunk_request` kế tiếp thì uplink bị mất.

---

## 6. Giới hạn cần biết

- **Không đo riêng được thời gian Bridge tải HTTP và nén zlib**, vì Bridge không publish gì khi tải xong. `init_request_to_first_chunk_request` chỉ là cận trên. Nếu cần đo chính xác, có thể thêm một bản tin vào `main.py` của Bridge.
- **Đếm mất bản tin là ước lượng** theo số lượng, không truy vết từng gói.
- **Chỉ theo dõi một thiết bị mỗi tiến trình**.
- Tool giả định gateway thêm `timestamp` ở cấp ngoài cùng của JSON (hoặc theo `ts_field`). Nếu thiếu, các chặng `dev_to_gw` và `gw_to_broker` để trống, các chặng còn lại vẫn tính được.
- **Độ chính xác**: đến cỡ vài chục ms, bị giới hạn bởi độ trễ UART/USB, broker và đồng bộ đồng hồ.
- Mất kết nối broker giữa chừng thì bản tin trong lúc rớt sẽ mất (tool tự kết nối lại).

## 7. Xử lý sự cố

| Hiện tượng | Cách xử lý |
|---|---|
| `cannot connect to MQTT broker` | Kiểm tra `broker`/`port`, tường lửa, tài khoản |
| `cannot open serial port` | Sai tên cổng, cổng đang bị chương trình khác giữ (monitor ESP32, IDE); WSL2 cần `usbipd-win` |
| `cannot open serial port ... Access is denied` (hoặc "could not open port") | Cổng đang bị `idf.py monitor`, IDE hoặc terminal khác giữ. Đóng chúng rồi chạy lại |
| Chạy rồi nhưng không có phiên nào | Có gửi lệnh Image tới đúng `<dev_id>/Command` không; `dev_id`/`deveui`/`devaddr` đã khớp chưa (bộ lọc quá chặt sẽ loại hết); thử để `devaddr` trống |
| Phiên có nhưng không có chunk từ serial | Regex không khớp log thật. Thử `python -c "import re; print(re.search(r'<regex>', '<một dòng log>'))"` |
| Chặng `dev_to_gw` rỗng | Gateway không có trường timestamp, hoặc `ts_field` sai |
| Có nhiều giá trị âm | Đồng hồ lệch, dùng `gw_offset_ms` |
| Hai máy cùng chạy mà luôn rớt kết nối | Trùng `client_id`; để trống `client_id` trong `profiler.ini` |
| Báo cáo không thấy ở đâu | `out_dir` tính theo thư mục hiện tại khi chạy lệnh, không phải thư mục của script |
