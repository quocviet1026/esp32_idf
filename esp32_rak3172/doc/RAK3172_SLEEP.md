# Cơ chế Sleep của module RAK3172 (LoRaWAN, RUI3 firmware)

Tài liệu này giải thích cơ chế ngủ/tiết kiệm pin phía **module RAK3172** khi ESP32 chạy ở sleep mode (xem tổng thể kiến trúc Deep Sleep của ESP32 tại `main/main.c`). Đây là phần RIÊNG cho LoRaWAN — mode WiFi/MQTT không dùng RAK3172 nên không liên quan tới tài liệu này.

## 1. Bối cảnh: RAK3172 KHÔNG bị cắt nguồn

Quyết định thiết kế đã chốt: board **không có mạch GPIO/MOSFET** để cắt nguồn module RAK3172 giữa các chu kỳ ESP32 Deep Sleep. Module được ESP32 cấp nguồn **liên tục**, kể cả khi ESP32 đang ngủ.

Hệ quả quan trọng: vì không bị cắt nguồn, RAM nội bộ của module (bao gồm session LoRaWAN đã join) **có khả năng sống sót** qua các lần ESP32 Deep Sleep — khác hẳn phía ESP32, vốn mất sạch RAM mỗi lần Deep Sleep (chỉ RTC memory sống sót, mà project hiện chưa dùng RTC memory).

Vì module không bị cắt nguồn, cơ chế tiết kiệm pin của nó phải dựa vào chính AT command nội tại của firmware RUI3 (`AT+LPM`/`AT+LPMLVL`), không phải cắt nguồn phần cứng.

## 2. AT command dùng cho sleep

| Lệnh | Ý nghĩa |
|---|---|
| `AT+LPM=1` | Bật chế độ **tự động ngủ giữa các lệnh AT** khi module đang rảnh (không có lệnh nào đang xử lý). Đây không phải lệnh "ngủ ngay bây giờ" (one-shot) — mà là bật 1 *chế độ hoạt động*, module tự lặp lại việc rơi vào sleep mỗi khi rảnh, không cần gọi lại lệnh này liên tục. |
| `AT+LPMLVL=1` | Chọn mức **STOP1** — cho phép đánh thức module qua hoạt động UART (có tín hiệu ở chân RX). Đây là lựa chọn **bắt buộc** cho thiết kế này: nếu dùng `AT+LPMLVL=2` (STOP2), tiết kiệm điện hơn nhưng **không đánh thức được qua UART**, ESP32 sẽ không thể "gọi" lại module ở chu kỳ thức tiếp theo. |
| `AT+NJS=?` | Hỏi module có đang giữ session join LoRaWAN hay không (trả `0`/`1`). Dùng để quyết định có cần `AT+JOIN` lại hay không mỗi lần ESP32 thức dậy. |

## 3. Cơ chế đánh thức — không có lệnh "wake" riêng

Vì dùng STOP1, UART của module có khả năng phát sinh ngắt đánh thức ngay khi có tín hiệu trên chân RX — **trước cả khi** module kịp hiểu byte đó là lệnh AT gì. Nên:

- **Không có API/lệnh AT riêng để "đánh thức"** module.
- **Bất kỳ byte nào** ESP32 ghi ra UART cũng tự động đánh thức module trước, sau đó nội dung lệnh mới được xử lý bình thường.
- Lệnh AT *đầu tiên* mà ESP32 gửi sau khi tự nó thức dậy (trong `rak3172_enable_low_power()`/`rak3172_configure_identity()`) chính là byte đóng vai trò đánh thức, đồng thời cũng là lệnh thật luôn — không phải 2 bước tách biệt.

Vì ESP32 tự Deep Sleep và reset hoàn toàn (không giữ được trạng thái UART driver qua các chu kỳ), thiết kế này **không cần đồng bộ thời gian ngủ chính xác giữa 2 chip** — không dùng `AT+SLEEP=<ms>` để tính khớp với chu kỳ `sleep_interval_ms` của ESP32. Module cứ tự ngủ khi rảnh, tự thức khi có hoạt động UART mới, bất kể ESP32 ngủ bao lâu.

## 4. Luồng thực tế mỗi chu kỳ

```
ESP32 Deep Sleep timer het han
  -> ESP32 reset hoan toan, boot lai tu dau
  -> app_main() -> transport->start() -> lorawan_start()
      -> rak3172_init()                    // khoi tao lai UART driver phia ESP32
      -> rak3172_enable_low_power()        // AT+LPM=1, AT+LPMLVL=1
                                            //   (byte dau tien nay tu danh thuc module neu no dang STOP1)
      -> rak3172_configure_identity(...)   // AT+NWM/BAND/NJM/DEVEUI/APPEUI/APPKEY/CLASS/CFM (idempotent)
      -> rak3172_query_join_status(&joined)// AT+NJS=?
         neu joined == true:
             -> bo qua AT+JOIN, goi thang on_lorawan_joined(true) -> transport_notify_ready()
         neu joined == false:
             -> rak3172_join()             // AT+JOIN=1:0:10:8 (async, cho +EVT:JOINED)
  -> (main.c) publish sensor + keepalive qua rak3172_send_uplink() (AT+SEND)
  -> cho DOWNLINK_GRACE_MS de nhan downlink (RX1/RX2 mo tu dong sau uplink)
  -> ESP32 lai Deep Sleep -> module tu ngu (AT+LPM=1 dang bat) cho toi chu ky sau
```

### File/hàm liên quan

| Vị trí | Vai trò |
|---|---|
| `components/rak3172/include/rak3172.h:92-110` | Khai báo + giải thích `rak3172_enable_low_power()` và `rak3172_query_join_status()`. |
| `components/rak3172/rak3172.c` (hàm `rak3172_enable_low_power`) | Gửi `AT+LPM=1` rồi `AT+LPMLVL=1`. |
| `components/rak3172/rak3172.c` (hàm `rak3172_query_join_status`) | Gửi `AT+NJS=?`, parse "0"/"1", tự đồng bộ `s_join_state = RAK3172_JOIN_JOINED` nếu phát hiện đã joined (để CLI `lorawan_status` báo đúng dù bỏ qua bước join). |
| `components/transport/transport_lorawan.c:58-120` (`lorawan_start()`) | Nơi gọi cả 2 hàm trên, theo đúng thứ tự ở mục 4. |

## 5. Cấu hình (CLI)

Sleep mode (ESP32 Deep Sleep) được bật/tắt và cấu hình chu kỳ qua CLI console — xem `components/cli_console/cmd_sleep_cfg.c`:

```
sleep_set <on|off> [<interval_ms>]   # bat/tat + chu ky thuc-ngu, can reboot de ap dung
sleep_show                            # in cau hinh sleep da luu
```

Mặc định `sleep_enabled=false` (không đổi hành vi hiện tại), `sleep_interval_ms=60000` (60s) nếu chưa từng đặt. Riêng phần low-power của **module RAK3172** (`AT+LPM`/`AT+LPMLVL`) **không có công tắc bật/tắt CLI riêng** — nó được bật cứng mỗi lần `lorawan_start()` chạy (tức là mỗi lần boot ở mode LoRaWAN), độc lập với `sleep_enabled` của ESP32. Lý do: bật `AT+LPM=1` là *best-effort*, không có hại gì kể cả khi ESP32 không bật Deep Sleep (module vẫn hoạt động bình thường, chỉ thêm 1 lớp tiết kiệm điện khi module rảnh giữa các lần gửi).

## 6. Rủi ro CHƯA xác nhận — bắt buộc bench test trước khi dùng production

Các mục này lấy nguyên văn từ comment trong code (`rak3172.h:92-101`) và plan thiết kế — **không suy đoán**, phải đo đạc thật trên phần cứng:

1. **Module có tự đợi hoàn tất giao dịch LoRaWAN xong mới thực sự vào STOP1 hay không?** `rak3172_send_uplink()` hiện tại (`AT+CFM=0`, unconfirmed uplink) chỉ đợi `OK` của lệnh `AT+SEND`, không có sự kiện "hoàn tất giao dịch" (TX xong + đóng cửa sổ RX1/RX2) nào để chờ thêm. Không có tài liệu RUI3 công khai nào xác nhận cơ chế auto-sleep của `AT+LPM=1` có gate theo trạng thái MAC/radio bận hay chỉ gate theo UART-idle đơn thuần. Nếu module ngủ giữa chừng giao dịch, uplink/downlink có thể bị mất mà **không có lỗi báo về phía ESP32**.
   → **Cách test**: đo dòng tiêu thụ thật của module (ampe kế/logic analyzer) trong khoảng từ lúc gửi `AT+SEND` tới khi cửa sổ RX2 đóng, xác nhận module không ngủ sớm trong khoảng này.
2. **`AT+NJS=?` có thực sự trả `1` sau khi ESP32 Deep Sleep rồi thức lại hay không** — tức module có thực sự giữ session OTAA qua các chu kỳ ngủ của ESP32 như kỳ vọng ở mục 1, hay vì lý do nào đó (watchdog nội bộ, giới hạn thời gian giữ session của network server...) module tự mất session dù không mất nguồn.
   → Nếu KHÔNG giữ được: bỏ nhánh "skip join" trong `lorawan_start()`, luôn `rak3172_join()` lại mỗi lần thức (tốn airtime/pin hơn nhưng an toàn).
3. Đo dòng tiêu thụ Deep Sleep tổng thể (ESP32 + RAK3172 ở chế độ LPM) để ước tính tuổi thọ pin thực tế theo `sleep_interval_ms` đã chọn.

## 7. Tài liệu tham khảo

- RAKwireless RUI3 AT Command Manual (tra cứu `AT+SLEEP`/`AT+LPM`/`AT+LPMLVL`/`AT+NJS`): https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual/
- Kế hoạch thiết kế đầy đủ (bao gồm phần ESP32 Deep Sleep, semaphore chờ transport ready, quy tắc an toàn với OTA rollback): `/home/vietnq/.claude/plans/crystalline-inventing-conway.md` (mục "Kế hoạch MỚI: Sleep mode tiết kiệm pin cho `esp32_rak3172`").
