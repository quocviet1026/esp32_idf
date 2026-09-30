# Phân tích Flash hiện tại — Project EMF (ESP32-C5)

> Kết quả đo đạc thực tế trên board thật, phục vụ thiết kế lại Partition Table để thêm tính năng OTA (xem kế hoạch chi tiết tại `/home/vietnq/esp/EMF/RETROFIT_OTA_ESP32C5.md`). Không có source code project lúc phân tích — toàn bộ số liệu lấy trực tiếp từ thiết bị qua cổng serial.

Ngày đo: 2026-09-22
Cổng serial: `/dev/ttyUSB0`

---

## 1. Thông tin chip & flash vật lý

Lệnh chạy:
```bash
esptool.py --port /dev/ttyUSB0 flash_id
```

Kết quả:
```
Chip is ESP32-C5 (revision v1.0)
Features: Wi-Fi 6 (dual-band), BT 5 (LE), IEEE802.15.4, Single Core + LP Core, 240MHz
Crystal is 48MHz
MAC: 3c:dc:75:ff:fe:85:75:24
BASE MAC: 3c:dc:75:85:75:24
Manufacturer: 20
Device: 4017
Detected flash size: 8MB
```

| Thông tin | Giá trị |
|---|---|
| Chip | ESP32-C5, revision v1.0 |
| Kiến trúc | RISC-V, Single Core + LP Core, 240MHz |
| Kết nối | WiFi 6 (dual-band 2.4/5GHz), BLE 5, IEEE 802.15.4 (Zigbee/Thread) |
| Flash vật lý thật | **8 MB** |
| MAC | `3c:dc:75:ff:fe:85:75:24` |

---

## 2. Partition table đang thực sự nằm trên thiết bị

Lệnh chạy:
```bash
esptool.py --port /dev/ttyUSB0 read_flash 0x8000 0xC00 partition-table-on-device.bin
python $IDF_PATH/components/partition_table/gen_esp32part.py partition-table-on-device.bin
```

Kết quả:
```
# ESP-IDF Partition Table
# Name, Type, SubType, Offset, Size, Flags
nvs,data,nvs,0x9000,24K,
phy_init,data,phy,0xf000,4K,
factory,app,factory,0x10000,2M,
```

| Partition | Type | SubType | Offset | Size |
|---|---|---|---|---|
| `nvs` | data | nvs | `0x9000` | 24 KB |
| `phy_init` | data | phy | `0xf000` | 4 KB |
| `factory` | app | factory | `0x10000` | 2 MB |

**Nhận xét:** đây là partition table **mặc định dạng single-app** (chưa có `otadata`, chưa có ≥2 app partition) — đúng như mô tả ban đầu, chưa hỗ trợ OTA. Tổng dung lượng đã cấp phát: `0x10000 + 0x200000 = 0x210000` (~2.06 MB) trong tổng 8MB flash → còn **~5.94 MB hoàn toàn chưa cấp phát cho partition nào**.

File dump `partition-table-on-device.bin` (3072 byte) đang lưu tại `/home/vietnq/esp/EMF/`.

---

## 3. Phân tích app image đang chạy trong `factory`

Vì không có source code để chạy `idf.py build`, đọc trực tiếp nội dung partition `factory` từ thiết bị rồi phân tích bằng `esptool.py image_info`:

```bash
esptool.py --port /dev/ttyUSB0 read_flash 0x10000 0x200000 factory_dump.bin
esptool.py image_info -v 2 factory_dump.bin
```

Kết quả đầy đủ:
```
File size: 2097152 (bytes)
Detected image type: ESP32-C5

ESP32-C5 image header
=====================
Image version: 1
Entry point: 0x408002b2
Segments: 6
Flash size: 8MB
Flash freq: 80m
Flash mode: DIO

ESP32-C5 extended image header
==============================
WP pin: 0xee (disabled)
Chip ID: 23 (ESP32-C5)
Minimal chip revision: v1.0, (legacy min_rev = 0)
Maximal chip revision: v1.99

Segments information
====================
Segment   Length   Load addr   File offs  Memory types
-------  -------  ----------  ----------  ------------
      0  0x33300  0x420f0020  0x00000018  DROM, IROM
      1  0x0ccf0  0x40800000  0x00033320  DRAM, BYTE_ACCESSIBLE, IRAM
      2  0xea830  0x42000020  0x00040018  DROM, IROM
      3  0x0ffb0  0x4080ccf0  0x0012a850  DRAM, BYTE_ACCESSIBLE, IRAM
      4  0x04a78  0x4081cd00  0x0013a808  DRAM, BYTE_ACCESSIBLE, IRAM
      5  0x000a4  0x50000000  0x0013f288  RTC_IRAM, RTC_DRAM

ESP32-C5 image footer
=====================
Checksum: 0xa8 (valid)
Validation hash: 9c87f8c664abd96a6c76a6ab8a2f4368fbec57c78290f3e6f47280b15c669547 (valid)

Application information
=======================
Project name: ESL
App version: v1.0.0-1-g0bd2023
Compile time: Jun  9 2026 13:42:29
ELF file SHA256: ff237b04c4536e24444ce2b9d9d31953b7968c7a1ce57677587b901990ff41dd
ESP-IDF: v5.5.1
Minimal eFuse block revision: 0.0
Maximal eFuse block revision: 0.99
MMU page size: 64 KB
Secure version: 0
```

| Thông tin | Giá trị |
|---|---|
| Project name | ESL |
| App version | `v1.0.0-1-g0bd2023` (đã có git-based versioning qua `PROJECT_VER`/`git describe`) |
| ESP-IDF dùng để build firmware hiện tại | **v5.5.1** |
| Compile time | Jun 9 2026 13:42:29 |
| Secure Boot / Secure OTA | Chưa bật (`Secure version: 0`, không có "Secure Boot" trong header) |
| Số segment | 6 |

File dump `factory_dump.bin` (2097152 byte = đúng 2MB của partition) đang lưu tại `/home/vietnq/esp/EMF/`. **Lưu ý:** phần lớn cuối file là `0xFF` (vùng flash trống, chưa từng ghi) — kích thước file KHÔNG phản ánh dung lượng app thật, phải tính từ segment cuối (xem mục 4).

### Tính dung lượng app THẬT sự đang dùng

Công thức:
```
size_that = lam_tron_len_boi_16(offset_segment_cuoi + length_segment_cuoi + 1_byte_checksum)
          + 32_byte (vi co dong "Validation hash" - anh co dinh kem SHA256)
```

Áp dụng với segment cuối (index 5): `File offs = 0x0013f288`, `Length = 0x000a4`:
```
0x13f288 + 0xa4 + 1          = 0x13F32D
làm tròn lên bội 16           = 0x13F330
+ 32 byte SHA256              = 0x13F350
```

**→ Dung lượng app thật đang dùng ≈ `0x13F350` = 1,309,008 bytes ≈ 1.25 MB** (trong tổng 2MB của `factory` → **62% đã dùng, 38% còn trống**).

---

## 4. Tổng hợp số liệu dùng để thiết kế lại Partition Table

| Hạng mục | Giá trị |
|---|---|
| Flash vật lý thật | 8 MB (`0x800000`) |
| Đã cấp phát (bảng hiện tại) | ~2.06 MB (`nvs` + `phy_init` + `factory`) |
| Chưa cấp phát (còn trống) | ~5.94 MB |
| App thật đang dùng | ~1.25 MB (62% của `factory` 2MB) |
| ESP-IDF version | v5.5.1 (đủ mới cho target `esp32c5`) |
| Secure Boot / Secure OTA | Chưa bật — cần thiết lập mới khi thêm OTA |

**Kết luận:** đủ điều kiện để thiết kế partition table mới với 2 OTA slot **2MB mỗi slot** (margin ~38% so với dung lượng đang dùng, đủ chỗ cho code OTA thêm vào + tăng trưởng tính năng), giữ nguyên `nvs`/`phy_init` không đổi, còn dư ~3.875MB cho nhu cầu khác trong tương lai — chi tiết đầy đủ (công thức, mẫu `partitions.csv`, các bước tiếp theo) xem tại `/home/vietnq/esp/EMF/RETROFIT_OTA_ESP32C5.md` mục 2.

---

## 5. File `partitions.csv` đã tạo sẵn — cách dùng

Dựa đúng số liệu ở mục 4, đã tạo sẵn file **`partitions.csv`** ngay trong thư mục này (`/home/vietnq/esp/EMF/partitions.csv`):

```csv
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   24K,
phy_init,   data, phy,     0xf000,   4K,
otadata,    data, ota,     0x10000,  8K,
ota_0,      app,  ota_0,   0x20000,  2M,
ota_1,      app,  ota_1,   0x220000, 2M,
```

**Đã validate hợp lệ** bằng chính công cụ ESP-IDF (không lỗi chồng lấn/căn chỉnh offset):
```bash
python $IDF_PATH/components/partition_table/gen_esp32part.py partitions.csv partitions_validated.bin
```
Lệnh này chạy thành công (exit code 0), không báo lỗi gì — xác nhận file CSV đúng cú pháp và các partition không đè lên nhau trước khi đưa vào project thật.

### Cách áp dụng vào project EMF khi có source code

1. **Copy file** `partitions.csv` này vào **thư mục gốc** của project EMF (ngang hàng với `CMakeLists.txt` cấp cao nhất, giống cách `sdkconfig` đang nằm).

2. Trỏ project dùng đúng file này:
   ```bash
   idf.py menuconfig
   # → Partition Table → chọn "Custom partition table CSV"
   # → xác nhận filename = "partitions.csv" (mặc định đã đúng nếu đặt tên như trên)
   ```
   Đồng thời xác nhận `Serial flasher config → Flash size` = `8 MB` (khớp số đo thật ở mục 1) — sai chỗ này gây lỗi ghi flash sai phạm vi.

3. **Bật App Rollback** ngay lúc này luôn (cùng lúc đổi partition table, đỡ phải mở lại menuconfig sau — xem Bước 3 trong `RETROFIT_OTA_ESP32C5.md`):
   ```
   idf.py menuconfig → Bootloader config → Enable app rollback support
   ```

4. `idf.py build` — lúc này bản build đầu tiên sẽ nhắm tới partition **`ota_0`** (không phải `factory` như bản đang chạy hiện tại, vì bảng mới không còn `factory`).

5. **Nạp qua cáp 1 lần duy nhất** (đúng như đã xác nhận thiết bị còn truy cập được):
   ```bash
   idf.py -p /dev/ttyUSB0 flash
   ```
   Lần nạp này ghi cả bootloader mới + partition table mới + app đầu tiên vào `ota_0` — **dữ liệu NVS cũ (`nvs` giữ nguyên offset/size) không bị ảnh hưởng**, nhưng `factory` cũ (2MB) sẽ không còn được bootloader mới nhận diện nữa (partition table mới không khai báo nó) — chấp nhận được vì đây chỉ là thay đổi 1 lần trong giai đoạn phát triển.

   > ⚠️ **Chú ý về sau (không chỉ riêng lần nạp đầu này): LUÔN dùng `idf.py flash` (full flash), KHÔNG dùng `idf.py app-flash`** khi thiết bị đã từng OTA thật qua mạng ít nhất 1 lần.
   >
   > Đã kiểm chứng trong source ESP-IDF (`app_update/CMakeLists.txt` + `esptool_py/CMakeLists.txt`):
   > - `idf.py flash` — mỗi lần chạy đều tự ghi đè `otadata` bằng 1 image trống (`blank_ota_data`), reset về trạng thái ban đầu (boot `ota_0`) — **luôn an toàn**.
   > - `idf.py app-flash` (lệnh flash nhanh, chỉ nạp lại app, hay dùng khi lặp code lúc dev) — **chỉ ghi app vào `ota_0`, KHÔNG đụng tới `otadata`**. Nếu lúc đó `otadata` đang trỏ `ota_1` (do đã OTA thật trước đó), bootloader **vẫn boot bản cũ ở `ota_1`**, phớt lờ app vừa nạp vào `ota_0` → ngồi debug tưởng code mới không có tác dụng, thực ra do nạp nhầm slot mà bootloader không chọn.
   > - Nếu lỡ dùng `app-flash` và gặp đúng tình huống trên: chạy `idf.py erase-otadata` (IDF v5+, alias cũ `erase_otadata` vẫn dùng được) để ép reset `otadata`, rồi khởi động lại.

6. Sau khi flash xong, chạy lại đúng 2 lệnh ở mục 2/3 tài liệu này (`read_flash` + `gen_esp32part.py`, `image_info`) để xác nhận bảng mới và app mới đã đúng như thiết kế.

### Bước tiếp theo

Sau khi áp dụng xong partition table này, các bước còn lại (Secure Boot V2 cho C5, viết component `ota_manager`, tuning hiệu năng, chọn kênh trigger, test bằng `otatool.py`) làm theo đúng thứ tự tại `/home/vietnq/esp/EMF/RETROFIT_OTA_ESP32C5.md` mục 3 trở đi.
