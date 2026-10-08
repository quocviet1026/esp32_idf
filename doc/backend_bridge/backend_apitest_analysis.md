# Backend (APItest.py): Phân tích nghiệp vụ, bản tin và luồng hoạt động

Tài liệu phân tích file `APItest.py`, một ứng dụng GUI (Tkinter + paho-mqtt) đóng vai **phía Backend** trong hệ thống LoRaWAN Bridge. Nội dung bám sát code đã cung cấp; chỗ nào suy ra từ các file bridge (`bridge.py`, `tlv.py`, `uplink.py`) đều có ghi chú.

> **Phạm vi:** `APItest.py` là **công cụ kiểm thử/mô phỏng backend**, không phải backend sản xuất. Nó gửi lệnh, nhận và hiển thị bản tin; không lưu trữ, không theo dõi trạng thái lệnh, không tự động hóa nghiệp vụ nào. Mục 12 nêu những gì một backend thật cần có thêm.
>
> **Chưa có:** tài liệu đặc tả *"Cummins Andon Tag/Server Communications"* mà docstring nhắc tới (nguồn gốc của các lệnh `Indicator`, `Menu`) và mã nguồn backend sản xuất (nếu có).

---

## 1. Vai trò trong hệ thống

```
 Thiết bị (Tag)      LoRaWAN NS         BRIDGE              Backend (APItest.py)
      |                  |                 |                         |
      |<--- LoRa ------->|<---- MQTT ----->|<-------- MQTT --------->|
                                           (broker Backend)
```

| Khái niệm | Trong `APItest.py` |
|---|---|
| Backend | Ứng dụng này |
| Tag | Thiết bị, định danh bằng **Tag Unique ID** |
| Tag Unique ID | Chính là **`DeviceID`** trong bridge (cũng là phần đầu của topic) |
| Broker | **Broker phía Backend** của bridge (`BACKEND_BROKER`), không phải broker LoRaWAN |
| Việc làm | Publish lệnh vào `{TagID}/Command`; subscribe `{TagID}/#` để xem mọi bản tin của thiết bị |

Ứng dụng chỉ làm việc với **một thiết bị tại một thời điểm** (một Tag ID).

---

## 2. Công nghệ và phụ thuộc

| Thành phần | Chi tiết |
|---|---|
| Giao diện | `tkinter` (cửa sổ 800x700), `ScrolledText` cho log |
| MQTT | `paho-mqtt`, dùng **API callback phiên bản 1** (`on_connect(client, userdata, flags, rc)`) |
| Luồng | Kết nối chạy trong thread riêng; paho chạy vòng lặp mạng bằng `loop_start()` |
| Client ID | `uuid.uuid4()` ngẫu nhiên mỗi lần kết nối |

**Lưu ý phiên bản paho:** với `paho-mqtt >= 2.0`, câu lệnh `mqtt.Client(client_id=...)` sẽ báo lỗi (bắt buộc truyền `callback_api_version`). Hoặc cài `paho-mqtt<2`, hoặc sửa thành:

```python
mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=str(uuid.uuid4()))
```

---

## 3. Kết nối MQTT

### 3.1 Tham số kết nối (từ GUI)

| Ô nhập | Mặc định | Ý nghĩa |
|---|---|---|
| Broker | `localhost` | Địa chỉ broker Backend |
| Port | `1883` | Cổng MQTT (không TLS, không xác thực) |
| Tag Unique ID | (trống, bắt buộc) | `DeviceID` của thiết bị cần điều khiển |

### 3.2 Quy trình kết nối

| Bước | Hành động trong code |
|---|---|
| 1 | Bấm **Connect**: đọc 3 ô, kiểm tra Tag ID không rỗng |
| 2 | Tạo thread: `connect_async(broker, port, 60)` rồi `loop_start()` |
| 3 | Chờ `connected_event` tối đa **5 giây** |
| 4 | Quá 5 giây: báo "Timed out", `loop_stop()`, bật lại nút Connect |
| 5 | `on_connect` với `rc == 0`: đổi trạng thái "Connected", **subscribe `{TagID}/#`** |
| 6 | `rc != 0`: báo `Connection failed with code {rc}` |
| 7 | Mất kết nối: `on_disconnect` cập nhật trạng thái; paho tự reconnect và `on_connect` subscribe lại |

Subscribe được đặt trong `on_connect` nên **sau reconnect vẫn nhận lại topic** (khác với bridge, nơi `subscribe` chỉ gọi một lần).

### 3.3 Topic và QoS

| Hướng | Topic | QoS | Ghi chú |
|---|---|---|---|
| Publish (gửi lệnh) | `{TagID}/Command` | **1** | |
| Subscribe (nhận) | `{TagID}/#` | 0 (mặc định) | Bao gồm cả `{TagID}/Command` do chính nó gửi |

Hai hệ quả:
- **Tự nhận lại bản tin của mình (echo):** vì `#` bao phủ `Command`, mỗi lệnh gửi đi sẽ xuất hiện thêm một lần trong log dưới dạng "Received message".
- **QoS thực tế tới bridge là 0:** broker chỉ giao QoS thấp hơn trong (QoS publisher, QoS subscription). Bridge gọi `subscribe("#")` mặc định QoS 0, nên lệnh QoS 1 từ đây vẫn có thể mất nếu bridge mất kết nối lúc đó.

---

## 4. Giao diện và chức năng

| Khu vực | Chức năng |
|---|---|
| **MQTT Connection Settings** | Nhập broker, port, Tag ID; nút Connect; nhãn trạng thái (đỏ/xanh) |
| **Send Commands** | Chọn lệnh trong danh sách; khung nhập tham số thay đổi theo lệnh; nút Send Command |
| **Activity Log** | Ghi mọi sự kiện kèm thời gian: kết nối, lệnh đã gửi, bản tin nhận về, lỗi |

Danh sách lệnh: `Ping`, `Reset`, `Shelf`, `Clear`, `Image`, `Indicator`, `Menu`.

Log hiển thị: kết nối, topic đã subscribe, **mã `mid` khi publish** (callback `on_publish`), nội dung lệnh đã gửi (JSON định dạng đẹp), và bản tin nhận (JSON định dạng đẹp; không phải JSON thì in nguyên văn).

---

## 5. Định dạng bản tin Backend gửi đi (Command)

### 5.1 Khung chung

Topic: `{TagID}/Command`, nội dung JSON:

```json
{
  "DeviceID": "shelf-01",
  "MessageID": 1,
  "Command": "Ping"
}
```

| Trường | Kiểu | Nguồn | Ràng buộc phía bridge |
|---|---|---|---|
| `DeviceID` | chuỗi | Ô Tag ID | **Phải bằng** `dev_id` trong topic, nếu không bridge bỏ lệnh |
| `MessageID` | số nguyên 32 bit | Bộ đếm nội bộ | Bắt buộc; bridge đổi thành 4 byte big-endian |
| `Command` | chuỗi | Danh sách chọn, viết hoa chữ đầu (`Ping`) | Bridge `strip().lower()` nên không phân biệt hoa/thường |
| `port`, `confirmed` | - | **Không gửi** | Bridge dùng mặc định `port=1`, `confirmed=false` |

### 5.2 `MessageID`

```python
self.message_id_counter = (self.message_id_counter + 1) & 0xFFFFFFFF
```

- Bắt đầu từ **1**, tăng dần, tràn quay về 0 sau `0xFFFFFFFF`.
- Bộ đếm **chỉ nằm trong RAM của chương trình**: tắt và mở lại thì quay về 1, nên `MessageID` bị **dùng lại** giữa các lần chạy.
- Một bộ đếm dùng chung cho mọi lệnh (không tách theo thiết bị hay theo loại lệnh).
- Mục đích: đối chiếu với `MessageID` trong bản tin `Handshake` thiết bị gửi về. **Công cụ không tự đối chiếu**, chỉ hiển thị.

### 5.3 `Ping`, `Reset`, `Shelf`

Không có tham số.

```json
{ "DeviceID": "shelf-01", "MessageID": 1, "Command": "Ping" }
```

### 5.4 `Clear`

| Trường | Kiểu | Mặc định | Ý nghĩa |
|---|---|---|---|
| `Refresh` | số nguyên | `0` | `0`: xóa màn hình ngay; số giây > 0: chờ rồi làm refresh thay vì clear |

```json
{ "DeviceID": "shelf-01", "MessageID": 4, "Command": "Clear", "Refresh": 0 }
```

### 5.5 `Image`

| Trường | Kiểu | Mặc định GUI | Ý nghĩa |
|---|---|---|---|
| `ImageHost` | chuỗi | `localhost` | Máy chủ HTTP chứa ảnh |
| `ImagePort` | số nguyên | `80` | Cổng HTTP |
| `ImageURL` | chuỗi | `/sample.epaper` | Đường dẫn file ảnh |
| `ImageHeight` | số nguyên | `0` | Chiều cao vùng vẽ (0 = toàn màn hình) |
| `ImageWidth` | số nguyên | `0` | Chiều rộng vùng vẽ (0 = toàn màn hình) |
| `ImageX` | số nguyên | `0` | Tọa độ X góc trên trái |
| `ImageY` | số nguyên | `0` | Tọa độ Y góc trên trái |

```json
{
  "DeviceID": "shelf-01",
  "MessageID": 5,
  "Command": "Image",
  "ImageHost": "192.168.1.50",
  "ImagePort": 8080,
  "ImageURL": "/epd/shelf01.bin",
  "ImageHeight": 0,
  "ImageWidth": 0,
  "ImageX": 0,
  "ImageY": 0
}
```

Điểm cần chú ý:
- **`ImageHost` là địa chỉ mà bridge sẽ truy cập để tải ảnh** (không phải máy chạy công cụ này). Giá trị mặc định `localhost` sẽ trỏ vào chính máy/container chạy bridge; nếu bridge chạy trong Docker thì thường sai.
- Cả 4 trường hình học (`Height`, `Width`, `X`, `Y`) luôn được gửi, kể cả khi bằng 0.
- Quy tắc hình học do bridge kiểm tra khi thiết bị gửi `ImageRequest`: `Width` và `Height` phải cùng bằng 0 hoặc cùng khác 0; vùng không vượt `EPD_WIDTH`/`EPD_HEIGHT`.
- File ảnh phải đúng định dạng thô 4 bit/pixel và đúng kích thước mà bridge kỳ vọng (xem tài liệu bridge).

### 5.6 `Indicator`

Điều khiển đèn báo, đèn nháy (strobe) và còi. Chỉ các trường được điền mới có mặt trong JSON.

| Ô nhập | Khóa JSON | Điều kiện đưa vào |
|---|---|---|
| StrobeTime (ms) và StrobeFlash (ms) | `Indicator.Strobe.StrobeTime`, `Indicator.Strobe.StrobeFlash` | **Cả hai** phải có giá trị |
| LED1 đến LED4 (%) | `Indicator.LED1` đến `Indicator.LED4` | Mỗi ô độc lập, nếu có giá trị |
| Buzzer Duration (ms) và Buzzer Frequency (Hz) | `Indicator.Buzzer.DurationMs`, `Indicator.Buzzer.FrequencyHz` | **Cả hai** phải có giá trị |

```json
{
  "DeviceID": "shelf-01",
  "MessageID": 6,
  "Command": "Indicator",
  "Indicator": {
    "Strobe": { "StrobeTime": 5000, "StrobeFlash": 250 },
    "LED1": 100,
    "LED3": 50,
    "Buzzer": { "DurationMs": 1000, "FrequencyHz": 2000 }
  }
}
```

Nếu để trống hết các ô, công cụ vẫn gửi `"Indicator": {}`. Không có kiểm tra phạm vi (ví dụ LED 0 đến 100).

### 5.7 `Menu`

Nội dung là một JSON tự do do người dùng soạn trong ô văn bản; công cụ chỉ kiểm tra JSON hợp lệ rồi gắn vào khóa `Menu`.

```json
{
  "DeviceID": "shelf-01",
  "MessageID": 7,
  "Command": "Menu",
  "Menu": {
    "Title": "Main Menu",
    "Options": [
      { "id": "status_check", "text": "Status Check" },
      { "id": "settings_menu", "text": "Settings", "menuItems": [
          { "id": "wifi_settings", "text": "Wi-Fi" },
          { "id": "volume_settings", "text": "Volume" }
      ] }
    ]
  }
}
```

Cấu trúc: `Title` (chuỗi), `Options` là mảng các mục `{id, text}`, mục có thể chứa menu con qua `menuItems` (lồng nhiều cấp).

---

## 6. Đối chiếu lệnh với bridge LoRaWAN

Bridge (`on_downlink`) chỉ biết 5 lệnh. Hai lệnh còn lại của công cụ **không được hỗ trợ trên đường LoRa**:

| Lệnh GUI | Bridge xử lý? | TLV tạo ra | Kích thước khung* |
|---|---|---|---|
| `Ping` | Có | `DEVICE_ID`, `SERVER_MESSAGE_ID`, `COMMAND=1`, `CRC` | 22 byte |
| `Reset` | Có | như trên, `COMMAND=2` | 22 byte |
| `Shelf` | Có | như trên, `COMMAND=3` | 22 byte |
| `Clear` | Có | + `REFRESH` (4 byte) | 28 byte |
| `Image` | Có | + `IMAGE_HOST/PORT/URL/HEIGHT/WIDTH/X/Y` | **69 byte** (với giá trị mặc định GUI) |
| `Indicator` | **Không** | - | Bridge log `Unknown command: indicator`, bỏ lệnh |
| `Menu` | **Không** | - | Bridge log `Unknown command: menu`, bỏ lệnh |

\* Tính với `DeviceID = shelf-01` (8 ký tự). `DeviceID` dài hơn thì khung dài thêm tương ứng.

Hai lệnh `Indicator` và `Menu` có vẻ thuộc đặc tả MQTT/WiFi gốc (docstring nhắc tài liệu Cummins Andon Tag). Trên LoRa, lệnh phải được thêm vào cả `TLVCommand`, `TLVType` và bridge nếu cần hỗ trợ. Riêng `Menu` (JSON lồng nhiều cấp) thường **quá lớn** cho payload LoRaWAN.

### 6.1 Ví dụ khung TLV bridge sinh ra (DeviceID `shelf-01`)

| Lệnh | `MessageID` | Hex (gửi vào trường `data` của downlink) |
|---|---|---|
| Ping | 1 | `01087368656c662d3031100400000001680101ff012c` |
| Reset | 2 | `01087368656c662d3031100400000002680102ff012c` |
| Shelf | 3 | `01087368656c662d3031100400000003680103ff012c` |
| Clear (`Refresh=0`) | 4 | `01087368656c662d3031100400000004680104120400000000ff013a` |
| Image (mặc định GUI) | 5 | `01087368656c662d303110040000000568010513096c6f63616c686f737414020050080e2f73616d706c652e65706170657215020000160200001702000018020000ff0103` |

*(Các chuỗi này do tôi tính lại bằng đúng thuật toán trong `tlv.py`, chưa đối chiếu với thiết bị thật.)*

**Quan sát:** `Ping`, `Reset`, `Shelf` có cùng CRC `0x2c` dù `MessageID` và mã lệnh khác nhau. Lý do: mỗi lần chuyển sang lệnh kế tiếp thì `MessageID` tăng 1 **và** mã lệnh tăng 1, hai thay đổi này triệt tiêu nhau trong phép XOR. Đây là minh họa cho giới hạn của CRC XOR 1 byte: không phát hiện được kiểu thay đổi bù trừ như vậy.

### 6.2 Kích thước payload so với LoRaWAN

| Lệnh | Khung TLV | Với US915 (tham khảo) |
|---|---|---|
| Ping/Reset/Shelf | 22 byte | Lọt cả RX1 lẫn RX2 (RX2 mặc định DR8 giới hạn 53 byte) |
| Clear | 28 byte | Như trên |
| Image (mặc định GUI) | 69 byte | **Vượt 53 byte**, chỉ đến được nếu downlink đi qua RX1 (DR10 trở lên giới hạn 242 byte) |

Lệnh `Image` có `ImageHost` và `ImageURL` càng dài thì càng lớn. Dùng `ImageHost` dạng IP ngắn và URL ngắn sẽ an toàn hơn. Các giới hạn US915 trên là con số tham khảo theo đặc tả, cần đối chiếu với NS thực tế.

### 6.3 Giá trị vượt phạm vi

Bridge đổi số sang số byte cố định, không có `try/except`:

| Trường | Kích thước TLV | Giá trị tối đa | Quá giới hạn thì |
|---|---|---|---|
| `MessageID` | 4 byte | 4294967295 | `OverflowError`, callback `on_downlink` hỏng |
| `Refresh` | 4 byte | 4294967295 | như trên |
| `ImagePort`, `ImageHeight/Width/X/Y` | 2 byte | 65535 | như trên |

Công cụ chỉ kiểm tra "là số nguyên" (`int(...)`), **không kiểm tra phạm vi**, nên nhập giá trị lớn sẽ khiến lệnh bị hỏng ở bridge mà phía công cụ không biết.

---

## 7. Bản tin Backend nhận về (từ bridge)

Công cụ nhận mọi thứ dưới `{TagID}/#`. Bridge publish uplink của thiết bị lên topic `{DeviceID}/{topic_name}`, nội dung là JSON đã giải mã (không còn trường `topic_name`, không có `deveui`/`devaddr`).

> Định dạng chi tiết từng nghiệp vụ (khung TLV hex, phong bì NS, kiểm tra hợp lệ) nằm ở **mục 13**.
>
> Các JSON dưới đây là **ví dụ minh họa**. Tập trường thực tế do firmware quyết định; tên trường lấy từ `uplink.py`.

| Topic | Ý nghĩa | Trường chính |
|---|---|---|
| `{id}/Register` | Thiết bị đăng ký/khởi động | `DeviceID`, `DipSwitch`, `DeviceType`, `FirmwareVersion`, `qr_code`; `SSID`, `IPv4`, `IPv6` luôn rỗng |
| `{id}/Button` | Người dùng nhấn nút | `Button.ButtonName`, `Button.ButtonDurationMs`, `DipSwitch` |
| `{id}/Heartbeat` | Tín hiệu sống định kỳ | `Battery`, `rssi_dbm`, `timestamp`, `DipSwitch`... |
| `{id}/Handshake` | **Phản hồi lệnh** | `MessageID`, `Status`, `MessageNum` |
| `{id}/ImageRequest` | Yêu cầu ảnh hoặc xin chunk | `ImageHost/Port/URL/...`, `ChunkIndex`, `TotalChunks`, `ImageTransferStatus` |
| `{id}/Command` | **Echo** lệnh do chính công cụ gửi | Như mục 5 |

### 7.1 Ví dụ

`shelf-01/Button`:

```json
{ "Button": { "ButtonName": "A", "ButtonDurationMs": 350 }, "DeviceID": "shelf-01", "DipSwitch": "0x0A" }
```

`shelf-01/Handshake` (phản hồi lệnh `MessageID = 4`):

```json
{ "DeviceID": "shelf-01", "MessageID": 4, "Status": "Complete", "MessageNum": 12 }
```

`Status` nhận một trong: `Received`, `Complete`, `Failed` (hoặc dạng `0xNN` nếu giá trị lạ).

`shelf-01/ImageRequest`, hai dạng:

```json
{ "DeviceID": "shelf-01", "ImageHost": "192.168.1.50", "ImagePort": 8080, "ImageURL": "/epd/shelf01.bin",
  "ImageHeight": 0, "ImageWidth": 0, "ImageX": 0, "ImageY": 0 }
```

```json
{ "DeviceID": "shelf-01", "ChunkIndex": 5, "TotalChunks": 150 }
```

*(Dạng thứ hai: chunk request; có kèm `TotalChunks` hay không tùy firmware.)*

### 7.2 Cách công cụ hiển thị

`on_message`: thử `json.loads`; thành công thì in JSON định dạng đẹp; lỗi parse thì in chuỗi thô. Không phân loại, không lọc, không gắn kết quả với lệnh đã gửi.

**Nhiễu `ImageRequest`:** bridge publish **mọi** chunk request lên backend. Khi thiết bị đang nhận ảnh gồm N chunk, Activity Log sẽ nhận **N bản tin `ImageRequest`** (hàng trăm với chunk nhỏ), che mất các bản tin khác.

---

## 8. Luồng hoạt động

### 8.1 Kết nối

```
Người dùng           APItest.py                    Broker Backend
    |  nhập broker, port, TagID                         |
    |--- Connect ----->|                                |
    |                  |-- connect_async + loop_start ->|
    |                  |<------ CONNACK (rc=0) ---------|
    |                  |-- SUBSCRIBE  {TagID}/#  ------>|
    |<- "Connected" ---|                                |
```

### 8.2 Lệnh đơn giản (Ping/Reset/Shelf) và phản hồi

```
APItest.py        Broker        Bridge          NS/Gateway        Thiết bị
    |-- PUBLISH shelf-01/Command (QoS1) -->|        |                 |
    |<-- echo (do subscribe #) ------------|        |                 |
    |                  |------------------>|        |                 |
    |                                      | JSON -> TLV + CRC        |
    |                                      |-- downlink/{devaddr} --->|
    |                                      |                 | (chờ uplink kế tiếp)
    |                                      |                 |<-- uplink bất kỳ
    |                                      |                 |-- RX1/RX2 --> nhận lệnh
    |                                      |<------- uplink Handshake (MessageID, Status)
    |<-- shelf-01/Handshake ---------------|
```

| Bước | Mô tả |
|---|---|
| 1 | Người dùng chọn lệnh, bấm Send; công cụ dựng JSON với `MessageID` mới |
| 2 | Publish `{TagID}/Command` QoS 1; log ghi nội dung và `mid` |
| 3 | Bridge kiểm tra, tra Redis (`dev_id → deveui → devaddr`), dựng TLV, publish `downlink/{devaddr}` |
| 4 | **Chờ**: thiết bị Class A chỉ nhận downlink sau một uplink của chính nó (thường là Heartbeat) |
| 5 | Thiết bị thực thi và gửi `Handshake` (`Received`, rồi `Complete` hoặc `Failed`) |
| 6 | Công cụ hiển thị `Handshake` trong log; **người dùng tự đối chiếu `MessageID`** |

**Độ trễ:** thời gian từ lúc bấm Send đến lúc thiết bị phản hồi phụ thuộc chu kỳ uplink của thiết bị (có thể vài giây đến vài chục phút). Công cụ không có bộ đếm thời gian hay cảnh báo hết hạn.

### 8.3 Hiển thị ảnh (`Image`)

```
APItest.py     Bridge                     Thiết bị               Image HTTP server
    |-- Command Image ->|                      |                          |
    |                   |-- downlink (TLV) --->|                          |
    |                   |<-- uplink ImageRequest (host, port, url, ...)   |
    |<- shelf-01/ImageRequest                  |                          |
    |                   |-------------- GET http://host:port/url -------->|
    |                   |<------------------- file ảnh thô ---------------|
    |                   | kiểm tra, nén zlib, chia chunk                  |
    |                   |<-- uplink ImageRequest (ChunkIndex=0) --        |
    |<- shelf-01/ImageRequest                  |                          |
    |                   |-- downlink chunk 0 ->|                          |
    |                   |        ... lặp cho từng chunk ...               |
    |                   |                      | ghép, giải nén, vẽ lên EPD
```

| Giai đoạn | Việc của công cụ | Việc của hệ thống |
|---|---|---|
| Ra lệnh | Gửi lệnh `Image` một lần | Bridge chuyển tham số xuống thiết bị |
| Truyền ảnh | **Chỉ quan sát** (nhận các `ImageRequest`) | Bridge và thiết bị trao đổi chunk |
| Kết thúc | Không có thông báo hoàn tất rõ ràng nào do bridge phát | Có thể thấy `Handshake` hoặc `ImageTransferStatus` nếu firmware gửi |

**Điều kiện để chạy thành công** (người dùng lệnh này phải đảm bảo):
1. Có HTTP server chứa file ảnh thô đúng định dạng và kích thước.
2. `ImageHost`/`ImagePort` mà **bridge** truy cập được.
3. Thiết bị trong registry đã được đăng ký.

### 8.4 Quan sát đăng ký thiết bị

Công cụ không đăng ký thiết bị. Khi thiết bị khởi động và gửi `Register`, bridge ghi Redis rồi publish `{DeviceID}/Register`; công cụ (nếu đã Connect với đúng Tag ID) sẽ hiển thị bản tin này. Muốn gửi lệnh được, `DeviceID` phải đã có trong registry của bridge, nếu không bridge log `not found in registry` và bỏ lệnh mà công cụ không biết.

---

## 9. Bản đồ trường: từ GUI đến TLV

| Ô GUI | Khóa JSON | Kiểu TLV (type) | Kích thước | Thứ tự trong khung |
|---|---|---|---|---|
| (Tag ID) | `DeviceID` | `DEVICE_ID` (1) | chuỗi UTF-8 | 1 |
| (bộ đếm) | `MessageID` | `SERVER_MESSAGE_ID` (16) | 4 byte BE | 2 |
| Command | `Command` | `COMMAND` (104) | 1 byte (mã lệnh) | 3 |
| Refresh | `Refresh` | `REFRESH` (18) | 4 byte BE | 4 (Clear) |
| ImageHost | `ImageHost` | `IMAGE_HOST` (19) | chuỗi | 4 (Image) |
| ImagePort | `ImagePort` | `IMAGE_PORT` (20) | 2 byte BE | 5 |
| ImageURL | `ImageURL` | `IMAGE_URL` (8) | chuỗi | 6 |
| ImageHeight | `ImageHeight` | `IMAGE_HEIGHT` (21) | 2 byte BE | 7 |
| ImageWidth | `ImageWidth` | `IMAGE_WIDTH` (22) | 2 byte BE | 8 |
| ImageX | `ImageX` | `IMAGE_X` (23) | 2 byte BE | 9 |
| ImageY | `ImageY` | `IMAGE_Y` (24) | 2 byte BE | 10 |
| (tự động) | - | `CRC` (255) | 1 byte XOR | cuối |

Mã lệnh (`COMMAND`): `Ping=1`, `Reset=2`, `Shelf=3`, `Clear=4`, `Image=5`.

---

## 10. Phân tích chất lượng code

### 10.1 Điểm tốt

- Đơn giản, dễ dùng để thử nhanh từng lệnh.
- Subscribe trong `on_connect` nên tự đăng ký lại sau reconnect.
- Kết nối chạy thread riêng, GUI không bị đơ khi chờ broker.
- Client ID ngẫu nhiên, không đá nhau với client khác.
- Có xử lý `JSONDecodeError` cho cả Menu lẫn bản tin nhận.
- `MessageID` có xử lý tràn 32 bit.

### 10.2 Vấn đề

| # | Vấn đề | Hậu quả | Đề xuất |
|---|---|---|---|
| 1 | **Tkinter không an toàn thread**: `update_gui_status`, `log_message`, `connect_button.config` bị gọi từ thread paho và thread kết nối | Treo hoặc lỗi ngẫu nhiên, nhất là khi log nhiều (luồng `ImageRequest`) | Dùng `self.after(0, ...)` hoặc `queue.Queue` đọc bởi vòng `after` |
| 2 | Không tương thích `paho-mqtt >= 2.0` | Lỗi ngay khi bấm Connect | Ghim phiên bản hoặc truyền `CallbackAPIVersion` |
| 3 | `Indicator` và `Menu` không có đường qua bridge | Người dùng tưởng đã gửi, thiết bị không nhận gì | Cảnh báo trong GUI hoặc thêm hỗ trợ vào bridge/firmware |
| 4 | Không đối chiếu `MessageID` với `Handshake` | Phải so bằng mắt; dễ nhầm khi nhiều lệnh liên tiếp | Lưu bảng lệnh đang chờ, gắn trạng thái |
| 5 | `MessageID` về 1 mỗi lần mở lại chương trình | Trùng ID cũ, khó phân biệt phản hồi lệnh cũ/mới | Khởi tạo từ thời gian, hoặc lưu bộ đếm ra file |
| 6 | Không kiểm tra phạm vi số | Giá trị quá lớn làm bridge `OverflowError` | Giới hạn `ImagePort` 0 đến 65535, `Refresh` 0 đến 2^32-1... |
| 7 | Subscribe `{TagID}/#` nhận echo `Command` và dồn `ImageRequest` | Log nhiễu | Subscribe theo topic cụ thể, hoặc bộ lọc/ẩn `ImageRequest` chunk |
| 8 | `self.input_entries.get(key, tk.Entry())` tạo `Entry` rỗng mỗi lần, kể cả khi key có sẵn | Rò rỉ widget nhỏ, code khó đọc | Dùng `.get(key)` rồi kiểm tra `None`, hoặc truy cập `self.input_entries[key]` |
| 9 | Ô trống trong lệnh `Image` hoặc `Clear` | `int("")` ném lỗi, hiển thị chung chung "Failed to send command" | Kiểm tra từng ô, báo đúng ô lỗi |
| 10 | Không TLS, không xác thực MQTT | Chỉ phù hợp mạng nội bộ | Thêm user/password và TLS khi dùng thật |
| 11 | Không gửi `port`, `confirmed` | Luôn dùng mặc định bridge | Thêm tùy chọn nếu cần lệnh `confirmed` |
| 12 | `connected_event` chỉ chờ 5 giây; sau timeout gọi `loop_stop()` | Broker chậm thì phải bấm Connect lại | Tăng timeout hoặc để paho tự thử lại |
| 13 | `ensure_ascii` mặc định bật khi `json.dumps` | Tiếng Việt thành `\uXXXX` (vẫn hợp lệ, bridge `json.loads` giải mã lại đúng) | Không cần sửa; chú ý chuỗi UTF-8 dài hơn số ký tự khi tính payload LoRa |
| 14 | Không kiểm tra Tag ID có trong registry | Lệnh bị bridge bỏ im lặng | Theo dõi `{TagID}/Register`, hiển thị "thiết bị đã/chưa đăng ký" |

---

## 11. Quy trình kiểm thử đề xuất

| Bước | Việc làm | Kết quả mong đợi |
|---|---|---|
| 1 | Chạy bridge (một instance), Redis và broker | Bridge log kết nối thành công |
| 2 | Mở `APItest.py`, nhập broker Backend và Tag ID thật, Connect | Trạng thái "Connected", log "Subscribed to topic" |
| 3 | Khởi động thiết bị (hoặc reset) | Nhận `{id}/Register` trong log |
| 4 | Gửi `Ping` | Echo lệnh trong log; sau uplink kế tiếp của thiết bị, nhận `Handshake` với đúng `MessageID` |
| 5 | Gửi `Clear` (`Refresh=0`) | Màn hình xóa; `Handshake` `Complete` |
| 6 | Gửi `Image` với host/port bridge truy cập được | Chuỗi `ImageRequest` rồi ảnh hiện lên; xem log bridge `Image ready`, `Sent chunk` |
| 7 | Gửi `Indicator` hoặc `Menu` | **Dự kiến không có phản hồi**; bridge log `Unknown command` |
| 8 | Gửi lệnh với `DeviceID` sai/Tag không đăng ký | Bridge log `not found in registry` hoặc `DeviceID ... not match`, không có phản hồi |

Kiểm tra chéo ở bridge: `docker logs <bridge> | grep -E "Downlink received|sending the downlink|Unknown command|not found"`.

---

## 12. Gợi ý cho một backend thật

Công cụ này thiếu các phần mà backend sản xuất cần:

| Nhu cầu | Mô tả |
|---|---|
| **Theo dõi trạng thái lệnh** | Bảng `MessageID → (thiết bị, lệnh, thời điểm, trạng thái)` với vòng đời `Sent → Received → Complete / Failed / Timeout` |
| **Hết hạn lệnh** | Đặt timeout theo chu kỳ uplink của thiết bị (Class A), có chính sách gửi lại hoặc hủy |
| **ID bền vững, duy nhất theo thiết bị** | Lưu bộ đếm, không reset khi khởi động lại; tránh dùng lại ID đang chờ |
| **Quản lý thiết bị** | Lưu `Register` (DeviceType, FirmwareVersion, DipSwitch), trạng thái online dựa trên `Heartbeat` (pin, rssi) |
| **Lọc nhiễu ảnh** | Gom hoặc bỏ qua các `ImageRequest` có `ChunkIndex`; theo dõi tiến độ bằng `ChunkIndex/TotalChunks` và `ImageTransferStatus` |
| **Quản lý ảnh** | Server HTTP sinh ảnh `.bin` đúng định dạng EPD 4bpp, đúng kích thước; biết trước host/port bridge truy cập được |
| **Giới hạn LoRa** | Kiểm tra độ dài lệnh (đặc biệt `Image`) trước khi gửi; rút gọn host/URL |
| **Bảo mật** | Xác thực/TLS MQTT, kiểm soát ai được gửi lệnh `Image` (liên quan rủi ro SSRF phía bridge) |
| **Quan sát được** | Log cấu trúc, đối chiếu lệnh-phản hồi, cảnh báo thiết bị mất tín hiệu |
| **Phối hợp bridge** | Thống nhất tập lệnh hỗ trợ (`Indicator`, `Menu` nếu cần); xin bridge phát thêm bản tin lỗi (ví dụ `{id}/CommandError`) thay vì chỉ log |

---

## 13. Chi tiết định dạng bản tin theo từng nghiệp vụ

Mỗi nghiệp vụ được mô tả ở **ba tầng định dạng** mà một thông điệp đi qua:

```
Tầng 1  MQTT phía Backend   topic + JSON                    (APItest.py <-> bridge)
Tầng 2  MQTT phía LoRaWAN   topic + JSON phong bì           (bridge <-> Network Server)
Tầng 3  Khung TLV           chuỗi byte, hiển thị dạng hex   (nằm trong trường "data")
```

> **Về các ví dụ uplink** (Register, Heartbeat, Button, Handshake, ImageRequest): khung hex do tôi dựng theo đúng cách `uplink.py` giải mã. **Thứ tự TLV và tập trường thực tế do firmware quyết định**, tôi chưa có mã nguồn firmware, nên hãy đối chiếu với một gói thực tế. Các ví dụ downlink (Ping, Clear, Image, chunk) dựng theo đúng code của bridge. Toàn bộ CRC được tính bằng thuật toán XOR trong `tlv.py`.

### 13.1 Các phong bì chung

#### 13.1.1 MQTT phía Backend

| Thuộc tính | Uplink (bridge → backend) | Command (backend → bridge) |
|---|---|---|
| Topic | `{DeviceID}/{Register\|Button\|Heartbeat\|Handshake\|ImageRequest}` | `{DeviceID}/Command` |
| Payload | JSON UTF-8, một đối tượng phẳng (trừ `Button`) | JSON UTF-8, một đối tượng phẳng (trừ `Indicator`, `Menu`) |
| QoS | `BACKEND_QOS` (trong `config.py`, chưa biết giá trị) | 1 (công cụ APItest), nhưng bridge subscribe QoS 0 |
| Retain | Không (mặc định) | Không (mặc định) |
| Tên trường | PascalCase (`DeviceID`), ngoại lệ `rssi_dbm`, `timestamp`, `qr_code`, `crc_valid` | PascalCase |

#### 13.1.2 Phong bì uplink từ Network Server

Bridge subscribe `LORAWAN_UPLINK_TOPIC`, nhận JSON. Chỉ ba trường được dùng:

```json
{ "deveui": "70B3D57ED005AAAA", "devaddr": "01A98B34", "data": "<hex TLV>" }
```

| Trường | Kiểu | Bridge dùng làm |
|---|---|---|
| `deveui` | chuỗi hex 16 ký tự | Khóa registry và khóa `image_transfers` |
| `devaddr` | chuỗi hex 8 ký tự | Địa chỉ gửi downlink; ghi vào Redis |
| `data` | chuỗi hex | Đổi sang bytes (`raw`) rồi giải mã TLV |

Các trường khác của NS (rssi, snr, fcnt, port, dr...) nếu có thì chỉ được log, **không** chuyển lên backend.

#### 13.1.3 Phong bì downlink gửi Network Server

Topic `downlink/{devaddr}`, QoS 1:

```json
{ "devaddr": "01A98B34", "port": 1, "confirmed": false, "data": "<hex TLV>" }
```

| Trường | Giá trị |
|---|---|
| `devaddr` | Lấy từ Redis (lệnh) hoặc từ chính uplink (chunk) |
| `port` | Lệnh: `msg.get("port", 1)`; chunk: cố định 1 |
| `confirmed` | Lệnh: `msg.get("confirmed", False)`; chunk: cố định `false` |
| `data` | Chuỗi hex của khung TLV, **không** có tiền tố `0x`, không có dấu cách |

#### 13.1.4 Khung TLV

`| Type 1B | Length 1B | Value N byte |` nối liền, **TLV topic hoặc lệnh trước, TLV dữ liệu sau, TLV CRC (`ff 01 XX`) cuối cùng**. Số nhiều byte big-endian. Chi tiết ở mục 4 của tài liệu bridge.

### 13.2 Từ điển trường

| Khóa JSON | Kiểu JSON | TLV | Mã | Mã hóa Value | Hướng | Ghi chú |
|---|---|---|---|---|---|---|
| `DeviceID` | chuỗi | `DEVICE_ID` | 1 | UTF-8 | Cả hai | Khóa định danh logic; uplink dùng để đặt tên topic |
| `MessageNum` | số \| null | `MESSAGE_NUM` | 2 | uint16 BE | Uplink | `null` nếu Value ngắn hơn 2 byte |
| `Button.ButtonName` | chuỗi | `BUTTON_NAME` | 3 | UTF-8 | Uplink | Cần TLV `BUTTON` đứng trước |
| `Button.ButtonDurationMs` | số \| null | `BUTTON_DURATION` | 4 | uint32 BE | Uplink | `null` nếu ngắn hơn 4 byte |
| `DipSwitch` | chuỗi `"0xNN"` \| null | `DIP_SWITCH_STATE` | 5 | 1 byte | Uplink | Hiển thị hex 2 chữ số, viết hoa |
| (không decode) | - | `SWITCH_STATE`, `DEVICE_STATUS` | 6, 7 | - | - | Nếu thiết bị gửi sẽ thành `unknown_type_0x06/0x07` |
| `ImageURL` | chuỗi | `IMAGE_URL` | 8 | UTF-8 | Cả hai | |
| `qr_code` | chuỗi | `QR_CODE` | 9 | UTF-8 | Uplink | |
| `DeviceType` | chuỗi | `DEVICE_TYPE` | 10 | UTF-8 | Uplink | |
| `FirmwareVersion` | chuỗi | `FIRMWARE_VERSION` | 11 | UTF-8 | Uplink | |
| `Battery` | số thực \| null | `BATTERY_LEVEL` | 12 | float32 BE | Uplink | Làm tròn 2 chữ số; `null` nếu Value không đúng 4 byte |
| `rssi_dbm` | số nguyên | `SIGNAL_STRENGTH` | 13 | int8 | Uplink | RSSI do thiết bị tự đo, khác RSSI gateway |
| `timestamp` | số \| null | `TIMESTAMP` | 14 | uint32 BE | Uplink | Đơn vị do firmware quy định (dạng epoch giây là phổ biến, chưa xác nhận) |
| (không dùng) | - | `HANDSHAKE` | 15 | - | - | Chỉ có trong enum |
| `MessageID` | số \| null | `SERVER_MESSAGE_ID` | 16 | uint32 BE | Cả hai | Lệnh: backend → thiết bị; `Handshake`: thiết bị phản hồi lại |
| `Status` | chuỗi \| null | `COMMAND_STATUS` | 17 | 1 byte | Uplink | `Received`/`Complete`/`Failed`, hoặc `0xNN` |
| `Refresh` | số | `REFRESH` | 18 | uint32 BE | **Downlink** | Uplink không giải mã trường này |
| `ImageHost` | chuỗi | `IMAGE_HOST` | 19 | UTF-8 | Cả hai | |
| `ImagePort` | số \| null | `IMAGE_PORT` | 20 | uint16 BE | Cả hai | |
| `ImageHeight` | số \| null | `IMAGE_HEIGHT` | 21 | uint16 BE | Cả hai | 0 = toàn màn hình (khi cùng `ImageWidth` = 0) |
| `ImageWidth` | số \| null | `IMAGE_WIDTH` | 22 | uint16 BE | Cả hai | |
| `ImageX` | số \| null | `IMAGE_X` | 23 | uint16 BE | Cả hai | |
| `ImageY` | số \| null | `IMAGE_Y` | 24 | uint16 BE | Cả hai | |
| `ChunkIndex` | số \| null | `CHUNK_INDEX` | 25 | uint16 BE | Cả hai | Có mặt = chunk request |
| `TotalChunks` | số \| null | `TOTAL_CHUNKS` | 26 | uint16 BE | Cả hai | |
| (không decode) | - | `CHUNK_DATA` | 27 | byte thô | **Downlink** | Chỉ bridge gửi |
| `ImageTransferStatus` | số \| null | `IMAGE_TRANSFER_STATUS` | 28 | 1 byte (số thô) | Uplink | Ý nghĩa từng giá trị chưa rõ |
| (không dùng) | - | `COMPRESSED_SIZE` | 29 | - | - | Định nghĩa nhưng bridge không gửi |
| (nội bộ) | - | `CRC` | 255 | 1 byte (XOR) | Cả hai | `crc_valid` bị xóa trước khi publish |
| `Command` | chuỗi | `COMMAND` | 104 | 1 byte (mã lệnh) | **Downlink** | `Ping=1`, `Reset=2`, `Shelf=3`, `Clear=4`, `Image=5` |

Mã TLV topic (uplink): `Register=101`, `Button=102`, `Heartbeat=103`, `Handshake=105`, `ImageRequest=106`. TLV type lạ trong uplink được giữ dạng `"unknown_type_0xNN": "<hex>"`.

### 13.3 Nghiệp vụ A: Đăng ký thiết bị (`Register`)

**Mục đích:** thiết bị khai báo danh tính khi khởi động; bridge ghi registry và chuyển cho backend.

| Mục | Giá trị |
|---|---|
| Hướng | Thiết bị → Bridge → Backend |
| Topic backend | `{DeviceID}/Register` |
| TLV topic | `REGISTER` (101 = `0x65`) |
| TLV bắt buộc | `DEVICE_ID` (bridge cần để đăng ký) |
| TLV tùy chọn | `DIP_SWITCH_STATE`, `DEVICE_TYPE`, `FIRMWARE_VERSION`, `QR_CODE` |

**Khung TLV minh họa (35 byte):**

| Phần tử | Hex | Giải thích |
|---|---|---|
| `REGISTER` | `65 00` | TLV topic |
| `DEVICE_ID` | `01 08 73 68 65 6c 66 2d 30 31` | `shelf-01` |
| `DIP_SWITCH_STATE` | `05 01 0a` | 0x0A |
| `DEVICE_TYPE` | `0a 08 41 6e 64 6f 6e 54 61 67` | `AndonTag` |
| `FIRMWARE_VERSION` | `0b 05 31 2e 30 2e 33` | `1.0.3` |
| `CRC` | `ff 01 1c` | XOR |

```
650001087368656c662d303105010a0a08416e646f6e5461670b05312e302e33ff011c
```

**Phong bì NS (tầng 2):**

```json
{ "deveui": "70B3D57ED005AAAA", "devaddr": "01A98B34",
  "data": "650001087368656c662d303105010a0a08416e646f6e5461670b05312e302e33ff011c" }
```

**MQTT backend (tầng 1):** topic `shelf-01/Register`

```json
{
  "SSID": "",
  "IPv6": "",
  "IPv4": "",
  "DeviceID": "shelf-01",
  "DipSwitch": "0x0A",
  "DeviceType": "AndonTag",
  "FirmwareVersion": "1.0.3"
}
```

`SSID`, `IPv6`, `IPv4` luôn là chuỗi rỗng vì decoder khởi tạo chúng khi gặp TLV `REGISTER` và không có TLV nào ghi đè (di sản từ giao thức WiFi).

**Xử lý của bridge:**

| Điều kiện | Hành vi |
|---|---|
| Thiết bị chưa có trong Redis và `topic_name == "Register"` | `register_if_missing(deveui, DeviceID)` ghi 2 key; `dev_id` được gán |
| Thiết bị đã đăng ký, `DeviceID` giống | Không đổi |
| Thiết bị đã đăng ký, `DeviceID` **khác** | `update_device_id` (đổi tên), chỉ log warning |
| Thiếu `DEVICE_ID` | Gọi `register_if_missing(deveui, None)`, ghi dữ liệu rác (lỗi) |

Mọi trường hợp bridge đều `update_session(deveui, devaddr)` rồi publish lên backend.

### 13.4 Nghiệp vụ B: Tín hiệu sống (`Heartbeat`)

**Mục đích:** báo thiết bị còn hoạt động; mang pin và cường độ tín hiệu. **Đây cũng là uplink tạo cửa sổ nhận downlink cho Class A.**

| Phần tử | Hex | Giải thích |
|---|---|---|
| `HEARTBEAT` | `67 00` | TLV topic (103) |
| `DEVICE_ID` | `01 08 73 68 65 6c 66 2d 30 31` | `shelf-01` |
| `BATTERY_LEVEL` | `0c 04 40 6c cc cd` | float32 BE = 3.7 |
| `SIGNAL_STRENGTH` | `0d 01 ab` | 0xAB = 171, trừ 256 = **-85 dBm** |
| `TIMESTAMP` | `0e 04 68 e7 78 00` | uint32 = 1760000000 |
| `DIP_SWITCH_STATE` | `05 01 0a` | 0x0A |
| `CRC` | `ff 01 47` | XOR |

```
670001087368656c662d30310c04406ccccd0d01ab0e0468e7780005010aff0147
```

Khung 33 byte. MQTT backend, topic `shelf-01/Heartbeat`:

```json
{ "DeviceID": "shelf-01", "Battery": 3.7, "rssi_dbm": -85, "timestamp": 1760000000, "DipSwitch": "0x0A" }
```

Lưu ý định dạng:
- `Battery` là **float32**, bridge làm tròn 2 chữ số. Đơn vị (volt hay phần trăm) do firmware quy định, chưa xác nhận.
- `rssi_dbm` đọc như uint8 rồi trừ 256 nếu lớn hơn 127.
- Heartbeat không có xử lý đặc biệt ở bridge, chỉ chuyển tiếp.

### 13.5 Nghiệp vụ C: Nút bấm (`Button`)

| Phần tử | Hex | Giải thích |
|---|---|---|
| `BUTTON` | `66 00` | TLV topic (102) |
| `DEVICE_ID` | `01 08 ...` | `shelf-01` |
| `BUTTON_NAME` | `03 01 41` | `A` |
| `BUTTON_DURATION` | `04 04 00 00 01 5e` | 350 ms |
| `DIP_SWITCH_STATE` | `05 01 0a` | 0x0A |
| `CRC` | `ff 01 25` | XOR |

```
660001087368656c662d303103014104040000015e05010aff0125
```

MQTT backend, topic `shelf-01/Button` (khung 27 byte; giải mã từng bước ở mục 4.8 tài liệu bridge):

```json
{ "Button": { "ButtonName": "A", "ButtonDurationMs": 350 }, "DeviceID": "shelf-01", "DipSwitch": "0x0A" }
```

Điều kiện bắt buộc: TLV `BUTTON` phải đứng **trước** `BUTTON_NAME`/`BUTTON_DURATION`, nếu không decoder gặp `KeyError`.

### 13.6 Nghiệp vụ D: Lệnh điều khiển đơn giản (`Ping`, `Reset`, `Shelf`)

**Tầng 1: backend gửi** topic `shelf-01/Command`:

```json
{ "DeviceID": "shelf-01", "MessageID": 1, "Command": "Ping" }
```

**Kiểm tra của bridge** (theo thứ tự, vi phạm thì log và bỏ lệnh, không phản hồi):

| # | Điều kiện | Lỗi |
|---|---|---|
| 1 | Topic có đúng 2 phần và phần sau là `command` (không phân biệt hoa/thường) | Bỏ qua im lặng |
| 2 | `dev_id` có `devaddr` trong Redis | `not found in registry` |
| 3 | `DeviceID` trong JSON bằng `dev_id` trong topic | `DeviceID not found ... or not match` |
| 4 | Có `MessageID` | `MessageID not found` |
| 5 | Có `Command` là chuỗi (nếu thiếu, `.strip()` gây `AttributeError`) | Ngoại lệ không bắt |
| 6 | `Command` thuộc tập hỗ trợ | `Unknown command` |

**Tầng 3: khung TLV (22 byte)**

| Phần tử | Hex | Giải thích |
|---|---|---|
| `DEVICE_ID` | `01 08 73 68 65 6c 66 2d 30 31` | `shelf-01` |
| `SERVER_MESSAGE_ID` | `10 04 00 00 00 01` | MessageID = 1 |
| `COMMAND` | `68 01 01` | PING |
| `CRC` | `ff 01 2c` | XOR |

```
01087368656c662d3031100400000001680101ff012c
```

`Reset`: đổi `68 01 02` và `MessageID`; `Shelf`: `68 01 03`. Cấu trúc khung giống hệt nhau.

**Tầng 2: phong bì gửi NS**, topic `downlink/01A98B34`:

```json
{ "devaddr": "01A98B34", "port": 1, "confirmed": false, "data": "01087368656c662d3031100400000001680101ff012c" }
```

**Phản hồi của thiết bị** là bản tin `Handshake` (mục 13.12) mang đúng `MessageID`.

### 13.7 Nghiệp vụ E: Xóa/làm mới màn hình (`Clear`)

**Tầng 1:**

```json
{ "DeviceID": "shelf-01", "MessageID": 7, "Command": "Clear", "Refresh": 30 }
```

| Trường | Kiểu | Bắt buộc | Ý nghĩa |
|---|---|---|---|
| `Refresh` | số nguyên 0 đến 4294967295 | **Có** (thiếu thì bridge bỏ lệnh) | `0`: xóa ngay; `N > 0`: chờ N giây rồi refresh thay vì clear |

**Tầng 3 (28 byte):**

| Phần tử | Hex |
|---|---|
| `DEVICE_ID` | `01 08 73 68 65 6c 66 2d 30 31` |
| `SERVER_MESSAGE_ID` | `10 04 00 00 00 07` |
| `COMMAND` | `68 01 04` (CLEAR) |
| `REFRESH` | `12 04 00 00 00 1e` (30) |
| `CRC` | `ff 01 27` |

```
01087368656c662d303110040000000768010412040000001eff0127
```

Tầng 2 giống mục 13.6, chỉ thay `data`. Với `Refresh = 0`: `12 04 00 00 00 00` và CRC `0x3a` (khi `MessageID = 4`).

### 13.8 Nghiệp vụ F: Lệnh hiển thị ảnh (`Image`, downlink)

**Tầng 1:**

```json
{
  "DeviceID": "shelf-01", "MessageID": 5, "Command": "Image",
  "ImageHost": "192.168.1.50", "ImagePort": 8080, "ImageURL": "/epd/shelf01.bin",
  "ImageHeight": 0, "ImageWidth": 0, "ImageX": 0, "ImageY": 0
}
```

| Trường | Kiểu | Bắt buộc | Mặc định ở bridge | Ghi chú |
|---|---|---|---|---|
| `ImageHost` | chuỗi | **Có** | - | Là địa chỉ **bridge** sẽ tải ảnh |
| `ImagePort` | số 0 đến 65535 | **Có** | - | Vượt phạm vi gây `OverflowError` |
| `ImageURL` | chuỗi | **Có** | - | Bắt đầu bằng `/` (bridge ghép `http://host:port` + URL) |
| `ImageHeight` | số 0 đến 65535 | Không | 0 | |
| `ImageWidth` | số 0 đến 65535 | Không | 0 | Cùng 0 hoặc cùng khác 0 với `ImageHeight` |
| `ImageX`, `ImageY` | số 0 đến 65535 | Không | 0 | Vùng phải nằm trong `EPD_WIDTH` x `EPD_HEIGHT` |

**Tầng 3: khung TLV (74 byte)**

| Phần tử | Hex | Giải thích |
|---|---|---|
| `DEVICE_ID` | `01 08 73 68 65 6c 66 2d 30 31` | |
| `SERVER_MESSAGE_ID` | `10 04 00 00 00 05` | 5 |
| `COMMAND` | `68 01 05` | IMAGE |
| `IMAGE_HOST` | `13 0c 31 39 32 2e 31 36 38 2e 31 2e 35 30` | `192.168.1.50` |
| `IMAGE_PORT` | `14 02 1f 90` | 8080 |
| `IMAGE_URL` | `08 10 2f 65 70 64 2f 73 68 65 6c 66 30 31 2e 62 69 6e` | `/epd/shelf01.bin` |
| `IMAGE_HEIGHT` | `15 02 00 00` | 0 |
| `IMAGE_WIDTH` | `16 02 00 00` | 0 |
| `IMAGE_X` | `17 02 00 00` | 0 |
| `IMAGE_Y` | `18 02 00 00` | 0 |
| `CRC` | `ff 01 ee` | XOR |

```
01087368656c662d3031100400000005680105130c3139322e3136382e312e353014021f9008102f6570642f7368656c6630312e62696e15020000160200001702000018020000ff01ee
```

Kích thước = `38 + len(DeviceID) + len(ImageHost) + len(ImageURL)` byte (hằng số 38 gồm `MessageID` 6, `COMMAND` 3, `PORT` 4, bốn trường hình học 16, `CRC` 3, cộng 6 byte header của ba chuỗi). Với các giá trị trên: `38 + 8 + 12 + 16 = 74`.

**Phản ứng của thiết bị:** gửi uplink `ImageRequest` ban đầu (mục 13.9). Bridge **không tải ảnh ở bước này**.

### 13.9 Nghiệp vụ G: Yêu cầu ảnh ban đầu (`ImageRequest`, uplink)

| Mục | Giá trị |
|---|---|
| Nhận dạng | TLV topic `IMAGE_REQUEST` (106 = `0x6a`), **không có** `CHUNK_INDEX` |
| Topic backend | `{DeviceID}/ImageRequest` |
| Bridge làm gì | Chạy nền `handle_image_download`; vẫn chuyển bản tin lên backend |

**Khung TLV minh họa (67 byte):**

| Phần tử | Hex |
|---|---|
| `IMAGE_REQUEST` | `6a 00` |
| `DEVICE_ID` | `01 08 73 68 65 6c 66 2d 30 31` |
| `IMAGE_HOST` | `13 0c 31 39 32 2e 31 36 38 2e 31 2e 35 30` |
| `IMAGE_PORT` | `14 02 1f 90` |
| `IMAGE_URL` | `08 10 2f 65 70 64 2f 73 68 65 6c 66 30 31 2e 62 69 6e` |
| `IMAGE_HEIGHT/WIDTH/X/Y` | `15 02 00 00` `16 02 00 00` `17 02 00 00` `18 02 00 00` |
| `CRC` | `ff 01 f9` |

```
6a0001087368656c662d3031130c3139322e3136382e312e353014021f9008102f6570642f7368656c6630312e62696e15020000160200001702000018020000ff01f9
```

MQTT backend, topic `shelf-01/ImageRequest`:

```json
{
  "DeviceID": "shelf-01",
  "ImageHost": "192.168.1.50", "ImagePort": 8080, "ImageURL": "/epd/shelf01.bin",
  "ImageHeight": 0, "ImageWidth": 0, "ImageX": 0, "ImageY": 0
}
```

Bridge dùng chính các giá trị **trong uplink này** (không dùng lại giá trị của lệnh `Image`).

**Kiểm tra trước khi tải:**

| # | Điều kiện | Lỗi nếu vi phạm |
|---|---|---|
| 1 | Có `ImageHost` và `ImageURL` | `Missing ImageHost or ImageURL` |
| 2 | `Width` và `Height` cùng 0 hoặc cùng khác 0 | `Invalid partial image geometry` |
| 3 | Nếu có vùng: `x + w <= EPD_WIDTH`, `y + h <= EPD_HEIGHT` | `Partial image region out of bounds` |
| 4 | HTTP 200 | `Failed to download image: HTTP ...` |
| 5 | Kích thước file: toàn màn hình = `EPD_SCREEN_BYTES`; vùng con = `((w+1)//2) * h` | `Image byte size mismatch` |

Các lỗi đều chỉ được log, **không có bản tin báo lỗi** tới thiết bị hay backend.

### 13.10 Nghiệp vụ H: Truyền chunk ảnh

#### 13.10.1 Yêu cầu chunk (uplink)

| Nhận dạng | TLV topic `IMAGE_REQUEST` **và có** `CHUNK_INDEX` |
|---|---|
| Topic backend | `{DeviceID}/ImageRequest` (bridge vẫn chuyển lên cho mỗi chunk) |

Khung minh họa xin chunk số 5 (19 byte):

| Phần tử | Hex |
|---|---|
| `IMAGE_REQUEST` | `6a 00` |
| `DEVICE_ID` | `01 08 73 68 65 6c 66 2d 30 31` |
| `CHUNK_INDEX` | `19 02 00 05` |
| `CRC` | `ff 01 25` |

```
6a0001087368656c662d303119020005ff0125
```

Nếu firmware gửi thêm `TOTAL_CHUNKS` (ví dụ 150), khung dài 23 byte: `...19 02 00 05 1a 02 00 96 ff 01 ab`. MQTT backend, topic `shelf-01/ImageRequest`:

```json
{ "DeviceID": "shelf-01", "ChunkIndex": 5 }
```

hoặc kèm `"TotalChunks": 150`. **Bắt buộc có `DEVICE_ID`** trong chunk request, vì topic backend lấy từ `DeviceID` trong payload (nếu thiếu, topic thành `None/ImageRequest`).

#### 13.10.2 Trả chunk (downlink)

Không có `DEVICE_ID`, `SERVER_MESSAGE_ID`, `COMMAND`: thiết bị nhận diện qua `devaddr` của LoRaWAN.

| Phần tử | Kích thước | Mã hóa |
|---|---|---|
| `CHUNK_INDEX` (25) | 4 byte (2 + 2) | uint16 BE |
| `TOTAL_CHUNKS` (26) | 4 byte | uint16 BE |
| `CHUNK_DATA` (27) | `2 + n` byte, `n <= IMAGE_CHUNK_SIZE` | Dữ liệu zlib thô |
| `CRC` | 3 byte | XOR |

Ví dụ chunk 0 trong tổng 150 chunk, với 6 byte dữ liệu minh họa (bắt đầu `78 da`, đúng tiêu đề zlib mức nén 9):

```
190200001a0200961b0678daed9d3b0eff016f
```

| Phần tử | Hex |
|---|---|
| `CHUNK_INDEX` | `19 02 00 00` |
| `TOTAL_CHUNKS` | `1a 02 00 96` |
| `CHUNK_DATA` | `1b 06 78 da ed 9d 3b 0e` |
| `CRC` | `ff 01 6f` |

Phong bì NS: `port = 1`, `confirmed = false`. Với `IMAGE_CHUNK_SIZE = 200`, khung dài `200 + 13 = 213` byte. Cách tính vị trí chunk:

```
offset = ChunkIndex * IMAGE_CHUNK_SIZE
CHUNK_DATA = compressed[offset : offset + IMAGE_CHUNK_SIZE]   (chunk cuối có thể ngắn hơn)
TotalChunks = ceil(len(compressed) / IMAGE_CHUNK_SIZE)
```

**Kiểm tra của bridge:** có transfer cho `deveui`; chưa quá 20 phút; `ChunkIndex < TotalChunks`. Vi phạm thì log và bỏ, thiết bị không biết.

#### 13.10.3 Báo trạng thái truyền ảnh (uplink, nếu firmware gửi)

TLV `IMAGE_TRANSFER_STATUS` (28, 1 byte) trong uplink `ImageRequest`. Khung minh họa (18 byte):

```
6a0001087368656c662d30311c0101ff0127
```

MQTT backend: `{ "DeviceID": "shelf-01", "ImageTransferStatus": 1 }`. Bridge **không sử dụng** trường này (chỉ chuyển lên); ý nghĩa từng giá trị cần tra firmware.

### 13.11 Nghiệp vụ I: Lệnh `Indicator` và `Menu` (chỉ có tầng 1)

Hai lệnh này tồn tại ở phía backend (xem mục 5.6, 5.7) nhưng **chưa có định dạng TLV** và bridge không xử lý:

| Tầng | Trạng thái |
|---|---|
| 1. MQTT backend | Có (JSON `Indicator`, `Menu`) |
| 2/3. LoRaWAN, TLV | **Không có**: thiếu mã lệnh trong `TLVCommand`, thiếu mã TLV cho Strobe, LED, Buzzer, Menu |
| Bridge | `Unknown command`, bỏ lệnh |

Muốn hỗ trợ cần định nghĩa mã lệnh mới, mã TLV cho từng trường (hoặc một TLV nén), sửa `tlv.py`, `bridge.py` và firmware. `Menu` là JSON lồng nhiều cấp nên cần cách mã hóa gọn hoặc phân mảnh nhiều downlink.

### 13.12 Nghiệp vụ J: Phản hồi lệnh (`Handshake`, uplink)

**Mục đích:** thiết bị xác nhận đã nhận và đã thực thi lệnh; ghép với lệnh bằng `MessageID`.

| Phần tử | Hex | Giải thích |
|---|---|---|
| `HANDSHAKE` | `69 00` | TLV topic (105) |
| `DEVICE_ID` | `01 08 73 68 65 6c 66 2d 30 31` | |
| `SERVER_MESSAGE_ID` | `10 04 00 00 00 04` | Đối chiếu với `MessageID = 4` backend đã gửi |
| `COMMAND_STATUS` | `11 01 02` | 2 = Complete |
| `MESSAGE_NUM` | `02 02 00 0c` | 12 |
| `CRC` | `ff 01 36` | XOR |

```
690001087368656c662d30311004000000041101020202000cff0136
```

Khung 28 byte. MQTT backend, topic `shelf-01/Handshake`:

```json
{ "DeviceID": "shelf-01", "MessageID": 4, "Status": "Complete", "MessageNum": 12 }
```

Bảng `Status`:

| Mã | `Status` | Ý nghĩa gợi ý |
|---|---|---|
| 1 | `Received` | Đã nhận lệnh, chưa xong |
| 2 | `Complete` | Đã thực thi xong |
| 3 | `Failed` | Thực thi thất bại |
| khác | `0xNN` | Giá trị ngoài đặc tả |

Quan hệ lệnh và phản hồi: một lệnh có thể sinh **nhiều** `Handshake` (ví dụ `Received` rồi `Complete`), nên backend cần nhận nhiều phản hồi cho cùng `MessageID`. `MessageNum` là số thứ tự bản tin phía thiết bị (ý nghĩa chính xác chưa rõ).

### 13.13 Bảng tổng hợp kích thước (DeviceID `shelf-01`)

| Bản tin | Hướng | Khung TLV (byte) | US915 uplink DR0 (11) | DR1 (53) | DR2 (125) |
|---|---|---|---|---|---|
| `Register` (minh họa) | Uplink | 35 | Không | Có | Có |
| `Heartbeat` (minh họa) | Uplink | 33 | Không | Có | Có |
| `Button` | Uplink | 27 | Không | Có | Có |
| `Handshake` | Uplink | 28 | Không | Có | Có |
| `ImageRequest` ban đầu | Uplink | 67 | Không | **Không** | Có |
| Chunk request | Uplink | 19 đến 23 | Không | Có | Có |
| Khung tối thiểu (topic + `DeviceID` + `CRC`) | Uplink | 15 | **Không** | Có | Có |

| Bản tin | Hướng | Khung TLV (byte) | US915 downlink DR8 / RX2 (53) | DR10 trở lên / RX1 (242) |
|---|---|---|---|---|
| `Ping`, `Reset`, `Shelf` | Downlink | 22 | Có | Có |
| `Clear` | Downlink | 28 | Có | Có |
| `Image` (ví dụ mục 13.8) | Downlink | 74 | **Không** | Có |
| Chunk (`IMAGE_CHUNK_SIZE = 200`) | Downlink | 213 | **Không** | Có |

Các giới hạn US915 lấy theo đặc tả LoRaWAN Regional Parameters (tham khảo, cần đối chiếu với NS). Điều đáng chú ý: ở **DR0 (11 byte)** không bản tin nào của giao thức này gửi được, vì ngay cả khung tối thiểu cũng dài 15 byte với `DeviceID` 8 ký tự.

### 13.14 Ma trận kiểm tra hợp lệ

| Bản tin | Kiểm tra CRC | Kiểm tra trường bắt buộc | Kiểm tra phạm vi | Phản hồi lỗi |
|---|---|---|---|---|
| Mọi uplink | **Chỉ log**, vẫn xử lý | Không | Không | Không |
| `Register` | - | Không (thiếu `DeviceID` ghi rác) | Không | Không |
| Lệnh (downlink) | Thiết bị kiểm tra | Có (xem 13.6) | **Không** (`OverflowError`) | Không (chỉ log) |
| `ImageRequest` ban đầu | - | Có (host, URL) | Có (hình học, kích thước) | Không (chỉ log) |
| Chunk request | - | Có (transfer tồn tại, chỉ số hợp lệ) | Có | Không (chỉ log) |

Nhận xét: toàn bộ giao thức **không có kênh lỗi ngược**. Backend chỉ biết lệnh thất bại khi nhận `Handshake` với `Status = Failed`, hoặc khi hết thời gian chờ không có phản hồi.

---

## 14. Tóm tắt

- `APItest.py` là **bộ gửi lệnh và xem log** cho một thiết bị, kết nối tới broker Backend.
- Gửi lệnh qua `{DeviceID}/Command` với khung `{DeviceID, MessageID, Command, ...tham số}`; nhận mọi thứ qua `{DeviceID}/#`.
- Chỉ 5/7 lệnh (`Ping`, `Reset`, `Shelf`, `Clear`, `Image`) có đường xuống thiết bị qua bridge LoRa; `Indicator` và `Menu` bị bridge bỏ.
- Phản hồi lệnh nằm ở bản tin `Handshake` (`MessageID` + `Status`); công cụ không tự đối chiếu.
- Lệnh `Image` với giá trị mặc định tạo khung 69 byte, vượt 53 byte của RX2 US915 và chỉ đến được nếu downlink qua RX1.
- Các rủi ro chính của code: thao tác Tkinter từ thread khác, không tương thích paho 2.x, không kiểm tra phạm vi số, `MessageID` về 1 mỗi lần chạy.

## 15. Thông tin còn thiếu

| Cần | Để làm rõ |
|---|---|
| Tài liệu đặc tả Tag/Server Communications | Ý nghĩa đầy đủ `Indicator`, `Menu`; danh sách trường chính thức của từng bản tin |
| Mã nguồn firmware | Chu kỳ `Heartbeat`, thứ tự TLV, cách phản hồi `Handshake`, thiết bị có hỗ trợ `Indicator` qua LoRa không |
| Backend sản xuất (nếu có) | Cách theo dõi lệnh, quản lý ảnh, lưu trữ |
| `config.py` và `docker-compose.yml` của bridge | `BACKEND_BROKER`, `BACKEND_QOS`, mạng giữa bridge và image server |
