# Retrofit OTA vào 1 project ESP32-C5 đã có code chạy sẵn (chưa có OTA, dùng partition table default, chưa ra thị trường)

> Tài liệu này trả lời câu hỏi: "Có 1 project đã code sẵn, đang chạy trên ESP32-C5, chưa có OTA, dùng partition table mặc định (single app), thiết bị vẫn còn nạp lại được qua cáp (chưa ra thị trường) — làm sao thêm OTA vào **an toàn, ít tác động code cũ, hiệu năng tốt, kiến trúc khoa học**?"
>
> Khác với `OTA_PLAN.md` (project `esp/ota` được viết từ đầu, trên ESP32 gốc/Xtensa), đây là bài toán **retrofit** — chèn tính năng vào 1 codebase nghiệp vụ đã tồn tại, trên 1 chip khác (ESP32-C5, RISC-V). Các nguyên tắc kiến trúc và phần lớn API tái dùng được từ kinh nghiệm làm project `esp/ota`, nhưng có những ràng buộc và rủi ro **khác hẳn** cần xử lý riêng — đây chính là nội dung tài liệu này.

---

## 0. Rào cản nền tảng cần hiểu trước: đổi Partition Table không thể làm qua OTA

**✅ Đã xác nhận với người yêu cầu:** thiết bị **vẫn còn thể nạp lại qua cáp** (chưa ra thị trường, đang trong giai đoạn phát triển thêm tính năng này) — đúng tình huống thuận lợi nhất, toàn bộ quy trình dưới đây khả thi trọn vẹn. Mục này giữ lại để giải thích **vì sao** cần đúng 1 lần nạp lại qua cáp ở Bước 9, không nhảy thẳng vào code OTA ngay.

**Vì sao vẫn cần 1 lần nạp qua cáp dù không phải thiết bị đã ra field:** cơ chế OTA an toàn (Safe Update Mode) của ESP-IDF hoạt động bằng cách ghi firmware mới vào 1 **app partition khác** partition đang chạy (vd `ota_0` trong khi đang chạy `factory`), rồi mới chuyển con trỏ boot sang đó. Cơ chế này đòi hỏi **partition table phải ĐÃ CÓ SẴN** từ 2 app partition trở lên (`otadata` + ≥2 slot OTA) **trước khi** lần OTA đầu tiên diễn ra. Bản thân partition table lại là 1 phần dữ liệu flash mà OTA (`esp_https_ota`/`app_update`) **không có khả năng cập nhật** — đổi partition table thuộc nhóm "Unsafe Update" (theo đúng phân loại trong `docs/OTA_PLAN.md` mục "Safe vs Unsafe Update Mode"), không có cơ chế rollback nếu đổi giữa chừng bị lỗi. Vì vậy: partition table mới + bootloader mới + app đầu tiên đã tích hợp OTA **bắt buộc phải nạp qua USB/JTAG 1 lần** (Bước 9) — từ lần đó trở đi mọi bản cập nhật tiếp theo mới đi qua OTA được.

Vì thiết bị chưa ra thị trường, đây chỉ là **1 lần nạp lại bình thường trong quy trình phát triển** (giống mọi lần flash test khác), không phải rào cản thực sự — không cần lo phương án thu hồi thiết bị hay giới hạn cho lô sản xuất sau như trường hợp thiết bị đã ở ngoài field.

**✅ Đã xác nhận thêm:** ngoài nạp lại qua cáp, còn **truy cập được console/serial** (log, `idf.py monitor`, `otatool.py`) — nghĩa là toàn bộ quy trình test/debug đề xuất ở Bước 8 (`otatool.py` test Rollback qua serial, không cần mạng) và Bước 9 (theo dõi log lúc boot/OTA/rollback thật) đều thực hiện được đầy đủ, không bị giới hạn gì thêm. Đây là điều kiện lý tưởng để retrofit an toàn — mọi bước debug sâu (đọc `Guru Meditation Error`, backtrace, log rollback state...) đúng như đã minh hoạ thực tế trong `OTA_PLAN.md` đều khả dụng.

---

## 1. Baseline — làm trước khi động vào bất kỳ dòng code nào

Mục tiêu: có điểm mốc để so sánh/rollback nếu retrofit gây tác dụng phụ ngoài ý muốn lên phần code cũ.

1. **Tag/branch riêng** trong git cho trạng thái hiện tại (trước khi thêm OTA) — để luôn có đường quay lại nếu cần.

2. **Đo flash size vật lý thật** của board:
   ```bash
   # Đóng moi cua so idf.py monitor dang mo toi board truoc (port dang bi giu)
   esptool.py --port /dev/ttyUSBx flash_id
   ```
   Đọc dòng `Detected flash size: ...` — đây là dung lượng **thật**, độc lập với giá trị đang cấu hình trong `sdkconfig` (2 giá trị có thể lệch nhau nếu code cũ cấu hình sai/thận trọng hơn thực tế).

3. **Đọc đúng partition table ĐANG THỰC SỰ NẰM TRÊN THIẾT BỊ** (không chỉ tin vào source code — có thể lệch nếu lần flash gần nhất dùng config khác):
   ```bash
   # Vung partition table luon o offset 0x8000, kich thuoc co dinh 0xC00 (3KB)
   esptool.py --port /dev/ttyUSBx read_flash 0x8000 0xC00 partition-table-on-device.bin
   python $IDF_PATH/components/partition_table/gen_esp32part.py partition-table-on-device.bin
   ```
   Lệnh thứ 2 in ra đúng bảng CSV hiện tại (`nvs`, `phy_init`, `factory`/`app0`... kèm offset/size thật). Nếu không muốn đọc trực tiếp từ thiết bị, cách nhanh hơn (nhưng chỉ đúng nếu chưa có ai flash tay đổi khác source): `idf.py partition-table` in bảng theo đúng `sdkconfig` hiện tại của source code.

4. **Đo dung lượng app hiện tại đã dùng bao nhiêu byte** (số quan trọng nhất để quyết định kích thước 2 slot OTA ở Bước 2) — có 2 cách tùy có source code hay không:

   **Cách A — nếu có source code project:**
   ```bash
   idf.py build
   ```
   Đọc đúng dòng cuối log build, dạng:
   ```
   app_hien_tai.bin binary size 0x9C4A0 bytes. Smallest app partition is 0x300000 bytes. 79% free.
   ```
   `0x9C4A0` chính là dung lượng app đang dùng thật.

   **Cách B — chỉ có thiết bị thật, KHÔNG có source code** (đọc trực tiếp app đang chạy ra từ flash rồi phân tích):
   ```bash
   # B1: doc toan bo partition app hien tai ra file (thay offset/size dung theo
   # bang partition that cua ban, lay tu Buoc 1.3 - vd offset 0x10000, size 2MB = 0x200000)
   esptool.py --port /dev/ttyUSB0 read_flash 0x10000 0x200000 factory_dump.bin

   # B2: phan tich image de lay size that (dung -v 2 de co day du segment + hash)
   esptool.py image_info -v 2 factory_dump.bin
   ```
   Lệnh B2 in ra danh sách **Segments** (mỗi dòng có `Length` và `File offs`) và mục **Application information** (project name, app version, ESP-IDF version dùng để build — tiện thể xác nhận luôn ESP-IDF version mà KHÔNG cần hỏi thêm). File dump (2MB) không phải là size thật — phần cuối file toàn `0xFF` (vùng flash trống chưa ghi) — phải tự tính:

   ```
   size_that = lam_tron_len_boi_16(offset_segment_cuoi + length_segment_cuoi + 1_byte_checksum)
             + 32_byte_neu_co_dong_"Validation hash"
   ```

   Ví dụ thực tế đã đo trên board ESP32-C5 8MB (chip `ESL`, ESP-IDF v5.5.1): segment cuối cùng `File offs 0x0013f288, Length 0x000a4` → `0x13f288 + 0xa4 + 1 = 0x13F32D`, làm tròn lên bội 16 → `0x13F330`, cộng 32 byte SHA256 → **`0x13F350` ≈ 1,309,008 bytes ≈ 1.25 MB** — đây là số dùng để tính margin ở Bước 2.

5. **Đo free heap hiện tại** lúc project đang chạy ổn định (`esp_get_free_heap_size()` log định kỳ, hoặc `idf.py monitor` + lệnh `heap` nếu có console) — vì thêm OTA sẽ tốn thêm RAM cho: buffer HTTP(S) (~4KB tuning), TLS context (mbedTLS tốn khá nhiều RAM, thường 30-40KB cho 1 kết nối TLS), task OTA riêng (8KB stack). Nếu heap hiện tại đã sát đáy, cần cân nhắc giảm buffer size hoặc tối ưu chỗ khác trước khi thêm OTA.

6. **Liệt kê toàn bộ NVS namespace / cấu hình đang dùng** — vì đổi partition table có thể cần đổi cả offset/size của NVS partition, ảnh hưởng tới dữ liệu NVS đã lưu trên thiết bị cũ (xem Bước 2). Nếu cần biết chính xác NVS đã dùng bao nhiêu (không chỉ đoán), gọi tạm trong code hiện tại (xoá sau khi đo xong):
   ```c
   nvs_stats_t stats;
   nvs_get_stats(NULL, &stats);
   ESP_LOGI(TAG, "NVS: used=%d free=%d total=%d entries", stats.used_entries, stats.free_entries, stats.total_entries);
   ```
   Mỗi entry chiếm 32 byte trong NVS — nhân `used_entries` lên để ước lượng byte thực dùng.

---

## 2. Thiết kế lại Partition Table — càng ít xáo trộn app hiện tại càng tốt

**Không dùng thẳng `CONFIG_PARTITION_TABLE_TWO_OTA` mặc định** như project `esp/ota` đã làm (project đó là greenfield, không có ràng buộc gì) — ở đây nên viết **custom `partitions.csv`**, vì 2 lý do:

- Bảng mặc định `two_ota` đặt offset app đầu tiên khác với offset `factory` mặc định của bảng `single_app` — nếu project hiện tại có bất kỳ chỗ nào đọc/ghi flash bằng offset cứng (hiếm nhưng vẫn nên kiểm tra), đổi bảng mặc định có thể làm hỏng.
- Custom CSV cho phép **giữ nguyên kích thước NVS/PHY/khác** đúng như hiện tại (giảm rủi ro mất dữ liệu NVS cũ khi thiết bị nhận bảng mới lần đầu), chỉ thêm mới phần cần thiết cho OTA (`otadata` 8KB + tách app hiện có thành `ota_0`/`ota_1`).

### Công thức tính kích thước `ota_0`/`ota_1` từ các số đã đo ở Bước 1

```
vung_con_lai_cho_app = flash_size_that - offset_bat_dau_nvs - size(nvs) - size(phy_init) - size(otadata)
kich_thuoc_moi_slot  = floor(vung_con_lai_cho_app / 2, lam_tron_xuong_boi_so_cua_0x10000)
```

**Lưu ý bắt buộc:** offset **và** size của mỗi app partition (`ota_0`, `ota_1`) phải là **bội số của 0x10000 (64KB)** — đây là yêu cầu cứng của ESP-IDF (do flash cache map theo trang 64KB), không phải tuỳ chọn. Nếu chia không tròn 64KB, ESP-IDF tự pad thêm khi build nhưng tốt nhất nên tự làm tròn xuống ngay trong CSV để kiểm soát chủ động.

**Kiểm tra điều kiện tối thiểu:** `kich_thuoc_moi_slot` phải **lớn hơn** con số "dung lượng app đang dùng" đo được ở Bước 1.4, cộng thêm biên độ cho:
- Code mới của `components/ota_manager` (cJSON + mbedTLS/esp-tls cho HTTPS + esp_https_ota) — thường cộng thêm **80-150KB** vào dung lượng binary so với bản chưa có OTA (tuỳ có sẵn WiFi/TLS trong code cũ hay chưa — nếu code cũ đã dùng WiFi+HTTPS rồi thì phần tăng thêm chủ yếu chỉ là `app_update`/`esp_https_ota`, nhỏ hơn nhiều).
- Dự phòng tăng trưởng tính năng nghiệp vụ sau này — khuyến nghị tối thiểu **+30%** so với dung lượng hiện tại.

### Số liệu THẬT đã đo trên board ESP32-C5 dùng cho retrofit này

Đo được bằng đúng 2 lệnh ở Bước 1.4 Cách B (không cần source code):

| Thông tin | Giá trị đo được |
|---|---|
| Flash thật | 8MB (`0x800000`) |
| Partition table hiện tại | `nvs` 24K @ `0x9000`, `phy_init` 4K @ `0xf000`, `factory` 2M @ `0x10000` |
| App đang dùng thật (tính từ `esptool.py image_info`) | ≈ 1.25 MB (`0x13F350` bytes) / trong 2MB của `factory` (62% used) |
| ESP-IDF dùng để build firmware hiện tại | v5.5.1 (đọc được từ `image_info`, không cần hỏi thêm) |

```
otadata (MỚI, cố định)      = 0x2000  (8KB)
vung_con_lai_cho_app        = 0x800000 - 0x9000 - 0x6000 - 0x1000 - 0x2000
                            = 0x7EE000  (= 8120 KB, còn dư rất nhiều so với flash 4MB điển hình)
```
Vì flash 8MB dư dả (chỉ mới dùng ~2.06MB / 8MB), **không cần chia đôi hết phần trống** — chọn mỗi slot **2MB** (bằng đúng size `factory` hiện tại, dễ kiểm chứng, margin ~38% so với 1.25MB đang dùng, đủ chỗ cho code `ota_manager` mới + tăng trưởng tính năng sau này), thay vì tính ra con số tối đa lý thuyết (~4MB/slot) — vừa đủ dùng, vừa để dành ~3.875MB hoàn toàn trống cho nhu cầu khác trong tương lai (vd thêm partition lưu trữ SPIFFS/LittleFS, hay coredump).

**partitions.csv áp dụng cho board thật này:**
```csv
# Name,     Type, SubType, Offset,   Size,     Comment
nvs,        data, nvs,     0x9000,   24K,      # giu nguyen y het hien tai
phy_init,   data, phy,     0xf000,   4K,       # giu nguyen y het hien tai
otadata,    data, ota,     0x10000,  8K,       # MOI - bat buoc cho OTA
ota_0,      app,  ota_0,   0x20000,  2M,
ota_1,      app,  ota_1,   0x220000, 2M,
```
Tổng dùng: `0x420000` (~4.125MB) / 8MB — còn dư ~3.875MB hoàn toàn trống, chưa cấp phát cho ai.

> ⚠️ Không đưa `factory` vào bảng mới — giữ đúng 2 slot `ota_0`/`ota_1` giúp logic rollback đơn giản hơn (không phải lo trường hợp đặc biệt "factory không rollback được" như đã ghi trong `OTA_PLAN.md`). Nghĩa là lần nạp qua cáp đầu tiên (Bước 9) sẽ ghi firmware vào `ota_0` (không phải `factory` như hiện tại).

Áp dụng: `idf.py menuconfig` → `Partition Table` → `Custom partition table CSV`, trỏ đúng file. Đồng thời xác nhận `Flash size` khớp giá trị đo thật (Bước 1.2) — sai chỗ này gây lỗi ghi đè flash ngoài phạm vi cho phép.

---

## 3. Bật App Rollback

`idf.py menuconfig` → `Bootloader config` → `Enable app rollback support` (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`).

Đây là bước **không tốn công sức** nhưng đổi lại toàn bộ lưới an toàn tự động (tự rollback nếu firmware mới crash/không confirm) — luôn bật, không có lý do để tắt.

### ⚠️ Lỗ hổng cần vá thêm: treo vô thời hạn nếu WiFi/MQTT không bao giờ kết nối được

Cơ chế rollback-khi-reboot-lại của bootloader chỉ kiểm tra state lúc có **1 lần BOOT MỚI** — không theo dõi liên tục. Nếu firmware OTA mới có bug khiến WiFi không bao giờ connect được (nhưng bản thân app **không crash**), thiết bị sẽ không bao giờ đạt checkpoint confirm (`ota_rollback_confirm_if_pending()`, thường gọi khi MQTT connect) **và** không có boot mới nào xảy ra để bootloader tự rollback — kẹt ở `ESP_OTA_IMG_PENDING_VERIFY` **vô thời hạn** cho tới khi có người rút nguồn thủ công. Vấn đề này càng đáng lo với thiết bị field thật (không có ai đứng cạnh để rút nguồn).

**Đã vá trong `ota_manager` phiên bản mới** (áp dụng luôn vào `esp/ota` gốc — xem `docs/OTA_PLAN.md` mục "Đồng hồ cảnh báo xác nhận rollback"): thêm `ota_rollback_start_confirm_watchdog(uint32_t timeout_ms)` — 1 `esp_timer` one-shot độc lập với Task Watchdog, gọi **ngay lúc boot, trước `wifi_manager_start()`**:

```c
ota_rollback_start_confirm_watchdog(2 * 60 * 1000);   // 2 phut, TRUOC wifi_manager_start()
wifi_manager_start();
```

Hết hạn mà vẫn chưa confirm → tự gọi `esp_ota_mark_app_invalid_rollback_and_reboot()` — chủ động rollback + reboot ngay, không chờ ai rút nguồn. Cần thêm `esp_timer` vào `PRIV_REQUIRES` của `components/ota_manager/CMakeLists.txt`. Giá trị 2 phút mới là điểm khởi đầu, **chưa kiểm chứng trên phần cứng C5 thật** — nên đo lại thời gian WiFi+MQTT connect thực tế trên board rồi đặt dư ra ít nhất gấp đôi.

---

## 4. Secure OTA — ⚠️ khác ESP32 gốc, hướng dẫn chi tiết từng bước cho C5

### 4.1 Vì sao khác ESP32 gốc

Project `esp/ota` (ESP32 Xtensa) dùng cơ chế **"Secure Signed Apps without Secure Boot"** với scheme **ECDSA (kiểu Secure Boot V1)** — chỉ verify chữ ký lúc **OTA update** (không phải Secure Boot phần cứng thật, không đụng eFuse, hoàn toàn reversible). Xem giải thích đầy đủ về sự khác biệt giữa cơ chế này và Secure Boot phần cứng thật trong `docs/OTA_PLAN.md` của `esp/ota`.

**ESP32-C5 (RISC-V) không hỗ trợ scheme ECDSA-V1 này** — đã verify trực tiếp trong Kconfig gốc ESP-IDF (`components/bootloader/Kconfig.projbuild`): option `SECURE_SIGNED_APPS_ECDSA_SCHEME` (ECDSA-V1) yêu cầu `depends on SECURE_BOOT_V1_SUPPORTED`, chip này chỉ có sẵn trên ESP32 gốc. Trên C5, `sdkconfig` chỉ show:
```
CONFIG_SECURE_BOOT_V2_RSA_SUPPORTED=y
CONFIG_SECURE_BOOT_V2_ECC_SUPPORTED=y
```
→ chỉ được chọn 1 trong 2 scheme: **RSA-3072** hoặc **ECDSA (định dạng "V2"**, khác key format với ECDSA-V1 của ESP32 gốc dù cùng tên thuật toán). Về bản chất **vẫn là cơ chế nhẹ/software/reversible y hệt** `esp/ota` — chỉ khác thuật toán ký.

### 4.2 Bước 1 — xác nhận chip revision (chỉ cần nếu định dùng RSA)

Kconfig gốc có 1 ràng buộc riêng cho C5:
```
config SECURE_SIGNED_APPS_RSA_SCHEME
    depends on !(IDF_TARGET_ESP32C5 && ESP32C5_REV_MIN_FULL < 1)
```
Nghĩa là scheme RSA-3072 **chỉ dùng được nếu chip revision của board thật ≥ 1**. Kiểm tra trên board thật:
```bash
idf.py -p /dev/ttyUSB0 monitor    # xem log boot, hoặc
espefuse.py -p /dev/ttyUSB0 summary | grep -i "chip revision"
```
Nếu revision < 1 (một số chip C5 đời đầu), **bắt buộc chọn ECDSA (V2)** ở bước sau thay vì RSA — không có lựa chọn khác.

### 4.3 Bước 2 — bật option trong menuconfig (đường dẫn + tên option chính xác)

```bash
idf.py set-target esp32c5
idf.py menuconfig
```
Vào `Security features`, bật/chọn theo đúng thứ tự sau (tên option lấy nguyên văn từ Kconfig):

1. **`Require signed app images`** (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`) — bật cái này trước, các option bên dưới mới hiện ra.
2. **`App Signing Scheme`** — menu chọn (radio), tùy kết quả Bước 4.2:
   - `RSA` (`SECURE_SIGNED_APPS_RSA_SCHEME`) — nếu chip rev ≥ 1.
   - `ECDSA (V2)` (`SECURE_SIGNED_APPS_ECDSA_V2_SCHEME`) — nếu không chắc hoặc chip rev < 1.
   - Nếu chọn ECDSA (V2), menu con **`ECDSA key size`** hiện thêm — chọn `NISTP256 (Recommended)` (`SECURE_BOOT_ECDSA_KEY_LEN_256_BITS`), trừ khi có lý do cụ thể cần 192/384-bit.
3. **`Verify app signature on update`** (`CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`) — **mặc định đã bật sẵn (`default y`)** khi bật mục 1, không cần đụng vào. Đây chính là phần verify chữ ký lúc OTA mà bạn muốn.
   - Lưu ý: option "verify chữ ký **mỗi lần boot**" (`SECURE_SIGNED_ON_BOOT_NO_SECURE_BOOT`) chỉ khả dụng với scheme ECDSA-V1 cũ — **không có trên C5**, nên trên C5 chữ ký chỉ được verify đúng 1 lần lúc OTA update, không verify lại mỗi lần khởi động (đây là hành vi mặc định phù hợp, không phải thiếu sót).
4. **`Sign binaries during build`** (`CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES`) — **mặc định đã bật sẵn (`default y`)** khi bật mục 1. Đây là option khiến `idf.py build` **tự động ký file `.bin`** ở cuối quá trình build, không cần chạy `espsecure.py sign_data` tay mỗi lần.
5. **`Secure boot private signing key`** (`CONFIG_SECURE_BOOT_SIGNING_KEY`) — đường dẫn tới file `.pem`, mặc định `secure_boot_signing_key.pem` (tương đối so với thư mục gốc project). Giữ mặc định này cho đồng bộ với `esp/ota`.
6. **Không** bật `Enable hardware Secure Boot in bootloader` (`CONFIG_SECURE_BOOT`) — giữ đúng tinh thần "without Secure Boot", không đụng eFuse.

Lưu lại (`S` → thoát menuconfig).

### 4.4 Bước 3 — tạo file khóa ký `.pem`

Dùng `espsecure.py` (có sẵn khi `source $IDF_PATH/export.sh`). Cú pháp thật (verify qua `espsecure.py generate_signing_key --help`):
```bash
espsecure.py generate_signing_key --version 2 --scheme <SCHEME> secure_boot_signing_key.pem
```
với `<SCHEME>` là một trong `rsa3072`, `ecdsa192`, `ecdsa256`, `ecdsa384` — chọn **đúng khớp** với lựa chọn ở Bước 4.3 mục 2. Ví dụ nếu chọn ECDSA(V2)/256-bit:
```bash
espsecure.py generate_signing_key --version 2 --scheme ecdsa256 secure_boot_signing_key.pem
```
File này chứa **private key** — dùng file này y hệt như đường dẫn đã khai ở `CONFIG_SECURE_BOOT_SIGNING_KEY` (mục 5 Bước 4.3), đặt ở gốc project (cùng cấp `CMakeLists.txt`), **giống hệt cách `esp/ota` đang làm**.

⚠️ **Chú ý bắt buộc:**
- `--version 2` vì C5 dùng Secure Boot V2 — dùng nhầm `--version 1` sẽ tạo key sai định dạng, build/sign sẽ lỗi hoặc verify sai.
- Thêm ngay file này vào `.gitignore` — private key **không bao giờ được commit vào git**.
- **Backup file này ở nơi an toàn riêng** (không phải trong repo) — mất file này thì không thể ký được firmware OTA mới nào nữa cho các thiết bị đã yêu cầu verify chữ ký (dù vẫn có thể **flash lại qua USB/UART bằng firmware build với option ký tắt đi**, vì đây không phải Secure Boot phần cứng — đây chính là điểm khác biệt "reversible" so với Secure Boot thật).

### 4.5 Bước 4 — build, kiểm tra binary đã được ký

```bash
idf.py build
```
Vì `Sign binaries during build` đã bật (Bước 4.3 mục 4), `build/<project>.bin` được **tự động ký** ở cuối quá trình build — không cần thao tác thêm. Kiểm tra xác nhận đã ký:
```bash
espsecure.py signature_info_v2 build/<ten_project>.bin
```
Lệnh này in ra thông tin block chữ ký V2 (version, kiểu key, public key digest...) — nếu binary **chưa** ký, lệnh sẽ báo lỗi không tìm thấy signature block. Đây là cách xác nhận chắc chắn thay vì đoán qua log build.

### 4.6 Bước 5 — test cơ chế từ chối chữ ký sai (mirror kịch bản đã test thật ở `esp/ota`)

Lặp lại đúng kịch bản `docs/OTA_PLAN.md` mục "Kịch bản test: Firmware OTA bị ký sai key" của `esp/ota`, áp dụng cho C5:
1. Build + flash bình thường 1 lần với key đúng — xác nhận app chạy, `ota_manager` hoạt động.
2. Tạo 1 key ký **khác** (`espsecure.py generate_signing_key ...` ra file `.pem` thứ 2), tạm đổi `CONFIG_SECURE_BOOT_SIGNING_KEY` trỏ sang key này, build ra 1 bản `.bin` "ký sai key".
3. Trigger OTA với bản `.bin` ký sai key này qua kênh MQTT hiện có → xác nhận `esp_https_ota`/`app_update` từ chối (`SIGNATURE_INVALID` hoặc lỗi tương đương), thiết bị **không** chuyển sang chạy firmware này.
4. Đổi lại `CONFIG_SECURE_BOOT_SIGNING_KEY` về đúng key thật trước khi build tiếp cho môi trường thật — tránh nhầm lẫn để sót key test.

### 4.7 Bảng tra nhanh Kconfig (đối chiếu esp/ota vs C5)

| Kconfig | `esp/ota` (ESP32 gốc) | `esp32c5_rak3172` (C5) |
|---|---|---|
| `SECURE_SIGNED_APPS_NO_SECURE_BOOT` | `y` | `y` (cần tự bật) |
| App Signing Scheme | `SECURE_SIGNED_APPS_ECDSA_SCHEME` (V1) | `SECURE_SIGNED_APPS_RSA_SCHEME` hoặc `SECURE_SIGNED_APPS_ECDSA_V2_SCHEME` |
| `SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT` | `y` | `y` (default, không cần đụng) |
| `SECURE_SIGNED_ON_BOOT_NO_SECURE_BOOT` | không bật (chỉ verify lúc update) | **không có lựa chọn này trên C5** (chỉ verify lúc update) |
| `SECURE_BOOT_BUILD_SIGNED_BINARIES` | `y` | `y` (default, không cần đụng) |
| `espsecure.py generate_signing_key` | `--version 1` (không cần `--scheme`) | `--version 2 --scheme rsa3072\|ecdsa192\|ecdsa256\|ecdsa384` |
| `CONFIG_SECURE_BOOT` (Secure Boot phần cứng thật) | tắt | tắt (giữ nguyên tinh thần "without Secure Boot") |

**Kiểm tra ESP-IDF version hỗ trợ C5:** xác nhận `idf.py --list-targets` có liệt kê `esp32c5`, và ESP-IDF đang dùng đủ mới (hỗ trợ C5 ổn định từ khoảng v5.3 trở lên) — nếu project hiện tại đang dùng bản ESP-IDF cũ hơn, cần nâng cấp ESP-IDF trước (1 thay đổi lớn, tách riêng khỏi việc thêm OTA, nên làm và test kỹ trước).

---

## 5. Kiến trúc code — đóng gói toàn bộ logic OTA thành 1 component độc lập, KHÔNG đụng code nghiệp vụ cũ

> **Giả định**: transport abstraction đã áp dụng xong theo `/home/vietnq/esp/EMF/RESTRUCTURE_TRANSPORT_ABSTRACTION.md` (project có `transport_if_t` với 2 implementation WiFi/MQTT và LoRaWAN, chọn 1 lúc boot qua NVS). Nếu chưa làm, làm tài liệu đó TRƯỚC — mục này giả định đã có sẵn implementation WiFi/MQTT để wire OTA vào.
>
> **OTA CHỈ tồn tại ở implementation WiFi/MQTT, KHÔNG có ở LoRaWAN** — LoRaWAN uplink/downlink chỉ vài chục-vài trăm byte/lần, không tải được firmware qua HTTPS. Đây không phải giới hạn runtime (không phải if/else chặn lại) mà là **không có lời gọi `ota_manager_start()` nào trên nhánh code LoRaWAN** — đúng nguyên tắc đã áp dụng nhất quán ở `esp32c5_rak3172`/`esp32_rak3172` (2 project tham chiếu, xem link bên dưới).

Áp dụng đúng API và kiến trúc **đã build sạch + wire thật** tại `/home/vietnq/esp/esp32c5_rak3172/components/ota_manager/` (copy nguyên gốc từ `esp/ota`, đã port sang C5 thành công) — KHÔNG dùng thiết kế "transport-agnostic" lý thuyết trước đây (`ota_manager_init/trigger/confirm_rollback_checkpoint`) vì thiết kế đó chưa từng được implement/test; API dưới đây là API THẬT, gắn với MQTT vì đó chính xác là những gì `esp_https_ota` cần (URL tải qua HTTPS, trạng thái báo qua MQTT topic) — không có lợi ích gì khi trừu tượng hoá thêm 1 lớp nữa phía trên transport abstraction đã có sẵn.

```
components/
└── ota_manager/                  (component MỚI, hoàn toàn tách biệt code cũ)
    ├── CMakeLists.txt
    ├── include/
    │   └── ota_manager.h
    ├── ota_manager.c             (nhận lệnh trigger qua MQTT, guard trùng lặp/version)
    ├── ota_rollback.c/.h         (xác nhận rollback tại checkpoint + watchdog chống treo)
    ├── ota_task.c/.h             (luồng esp_https_ota, tuning)
    └── server_certs/
        └── ca_cert.pem
```

API công khai thật (`ota_manager.h`, xem nguyên văn tại `/home/vietnq/esp/esp32c5_rak3172/components/ota_manager/include/ota_manager.h`):

```c
#define OTA_COMMAND_TOPIC "<namespace_rieng_cua_EMF>/ota/cmd"     // doi topic khop namespace MQTT hien co cua EMF
#define OTA_STATUS_TOPIC  "<namespace_rieng_cua_EMF>/ota/status"

// Goi 1 lan, ngay khi MQTT client cua transport WiFi/MQTT connect thanh cong.
void ota_manager_start(esp_mqtt_client_handle_t client);

// Nhan MOI MQTT_EVENT_DATA - ham tu kiem tra topic co khop OTA_COMMAND_TOPIC
// khong (bo qua neu khong khop), tu parse JSON {"version":..,"url":..}, tu
// guard (dang OTA do/version khong moi hon thi bo qua).
void ota_manager_handle_mqtt_data(const esp_mqtt_event_handle_t event);

// (khai bao trong ota_rollback.h, KHONG phai ota_manager.h) Goi tai checkpoint
// "da ket noi duoc server" cua tang ung dung (vd ngay sau MQTT_EVENT_CONNECTED)
// - neu dang o trang thai PENDING_VERIFY (vua OTA xong, dang cho xac nhan) thi
// danh dau app hop le, huy rollback.
void ota_rollback_confirm_if_pending(void);

// Dong ho canh bao doc lap (esp_timer) - goi TRUOC khi bat dau ket noi WiFi/MQTT.
// Neu qua timeout_ms van chua confirm duoc (vd WiFi/MQTT khong bao gio ket noi
// duoc du firmware moi khong crash), tu dong rollback+reboot thay vi treo vo
// thoi han - xem giai thich day du trong "Luo hong can va them" o Muc 3.
void ota_rollback_start_confirm_watchdog(uint32_t timeout_ms);

// Goi tu ota_task khi 1 lan OTA ket thuc ma KHONG reboot (tuc la that bai),
// de mo khoa cho lenh OTA tiep theo.
void ota_manager_notify_task_done(void);
```

**Điểm nối vào code cũ — chỉ trong `transport_wifi_mqtt.c` (hoặc file tương đương của implementation WiFi/MQTT sau khi làm xong transport abstraction), KHÔNG đụng gì tới `app_main()`/logic nghiệp vụ khác:**
1. Ngay trước khi bắt đầu connect WiFi (đầu hàm `start()` của transport WiFi/MQTT) → gọi `ota_rollback_start_confirm_watchdog(timeout_ms)`.
2. Trong callback `MQTT_EVENT_CONNECTED` → gọi `ota_manager_start(client)` rồi `ota_rollback_confirm_if_pending()`.
3. Trong callback `MQTT_EVENT_DATA` → gọi `ota_manager_handle_mqtt_data(event)` (hàm tự lọc đúng topic của nó, an toàn gọi vô điều kiện với mọi `MQTT_EVENT_DATA`).

Xem nguyên văn cả 3 điểm nối này đã wire thật trong `/home/vietnq/esp/esp32c5_rak3172/components/transport/transport_wifi_mqtt.c`.

**Vì sao thiết kế này "khoa học" theo đúng nghĩa kỹ sư nhúng lâu năm đánh giá:**
- **Single Responsibility**: component không biết gì về nghiệp vụ cũ, code cũ không biết chi tiết OTA hoạt động ra sao — giao tiếp qua đúng 5 hàm biên (API surface tối thiểu, đã liệt kê ở trên).
- **Không có shared mutable state** giữa code cũ và component mới ngoài đúng những gì đi qua tham số hàm — giảm tối đa nguy cơ side-effect không lường trước lên logic nghiệp vụ đang chạy ổn định.
- **Đã port thành công 3 lần** (`esp/ota` → `esp32c5_rak3172` → `esp32_rak3172`) với đúng 2-3 chỗ sửa mỗi lần (đổi tên topic, thay cert) — bằng chứng thực tế component này portable tốt, không phải lý thuyết.
- **Dễ test độc lập**: có thể viết unit test/tích hợp cho riêng `ota_manager` mà không cần dựng lại toàn bộ nghiệp vụ project cũ.

---

## 6. Tuning hiệu năng — áp dụng đúng, tránh lặp lại lỗi thật đã gặp

Khi viết `ota_task.c`, dùng đúng cấu hình đã kiểm chứng hoạt động ổn định trong `esp/ota`:

```c
esp_http_client_config_t http_config = {
    .url = url,
    .cert_pem = (char *)server_cert_pem_start,
    .timeout_ms = 10000,
    .keep_alive_enable = true,
    .buffer_size = 4096,
};

esp_https_ota_config_t ota_config = {
    .http_config = &http_config,
    .bulk_flash_erase = true,
    /* BẮT BUỘC phải có ca MALLOC_CAP_INTERNAL VA MALLOC_CAP_8BIT cung luc -
     * thieu MALLOC_CAP_8BIT gay Guru Meditation Error (LoadStoreError) THAT
     * tren phan cung (da gap va debug ky trong project esp/ota, xem
     * docs/KNOWN_ISSUES.md va OTA_PLAN.md muc "Su co da gap"). Ly do: thieu
     * co MALLOC_CAP_8BIT, bo cap phat duoc phep tra ve vung nho chi ho tro
     * truy cap 32-bit (kieu IRAM), trong khi cac struct header anh (esp_image_header_t)
     * co truong 2-byte can truy cap theo byte. */
    .buffer_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
};
```

**Riêng cho ESP32-C5 cần thêm 1 bước kiểm tra:** RISC-V single-core như C5 có ngân sách SRAM nội bộ khác ESP32 gốc (thường ít hơn) — **đo lại free heap sau khi bật OTA** (không chỉ tin số liệu tham khảo từ ESP32 gốc), đặc biệt nếu project cũ vốn đã dùng WiFi 6 + BLE + 802.15.4 đồng thời (nếu C5 dùng cho ứng dụng Matter/Thread) — heap có thể đã bị chia sẻ nhiều hơn so với 1 project chỉ dùng WiFi đơn thuần như `esp/ota`. Nếu heap căng, cân nhắc giảm `buffer_size` xuống 2048 trước khi tắt hẳn `bulk_flash_erase` (giảm buffer ít tốn heap hơn, trong khi tắt bulk erase chỉ đổi tốc độ, không đổi heap dùng).

Đồng thời tăng Task Watchdog timeout (`CONFIG_ESP_TASK_WDT_TIMEOUT_S`, vd 15s) như đã làm ở `OTA_PLAN.md` Bước 7, để bulk erase không trigger watchdog giả.

---

## 7. Chọn kênh trigger phù hợp với hạ tầng SẴN CÓ của project — không ép phải thêm MQTT nếu chưa cần

Project `esp/ota` dùng MQTT vì đó là lựa chọn thiết kế ban đầu của project đó. Với 1 project **đã chạy production**, ưu tiên nguyên tắc "ít tác động nhất": **tái dùng đúng kênh điều khiển từ xa project đã có sẵn** (nếu có) thay vì thêm hẳn 1 giao thức mới chỉ để phục vụ OTA:

| Nếu project cũ đã có sẵn | Cách tích hợp trigger OTA |
|---|---|
| Kết nối MQTT tới server riêng | Thêm 1 topic lệnh mới, gọi `ota_manager_trigger()` khi nhận — giống hệt cách đã làm ở `esp/ota` |
| Định kỳ gọi HTTP API về server (polling) | Server trả thêm field `{"ota_available": true, "version": "...", "url": "..."}` trong response sẵn có, code cũ đọc field này rồi gọi `ota_manager_trigger()` — không cần mở kết nối mới |
| Không có kênh điều khiển từ xa nào | Đây là lúc **thực sự cần** thêm 1 kênh mới — cân nhắc kỹ transport (MQTT/HTTP/CoAP) dựa theo hạ tầng backend đã có, và bắt buộc có xác thực ngay từ đầu (xem mục Checklist bảo mật bên dưới) — đừng lặp lại thiếu sót "broker công cộng ẩn danh" đã ghi nhận trong `KNOWN_ISSUES.md` mục 1. |

---

## 8. Test trên bench TRƯỚC khi đụng tới thiết bị field — dùng `otatool.py`

Trước khi dựng cả hạ tầng server + test OTA qua mạng thật, dùng `otatool.py` (component `app_update`, đã ghi trong `docs/ota/esp32-ota-tong-hop.md` mục 8) để test riêng logic Rollback/chọn slot **qua cáp serial, không cần mạng**:

```bash
otatool.py --port /dev/ttyUSBx erase_otadata
otatool.py --port /dev/ttyUSBx switch_ota_partition --slot 1
```

Việc này cho phép xác nhận toàn bộ `ota_manager`/`ota_rollback` hoạt động đúng **độc lập với hạ tầng mạng**, cách ly biến số, đúng tinh thần "an toàn, ít rủi ro" khi retrofit vào code đang chạy ổn định.

---

## 9. Rollout thực tế — theo giai đoạn, không đại trà toàn bộ fleet cùng lúc

1. Flash bản có OTA lên **1 board bench** trước, chạy ổn định vài ngày (không chỉ test nhanh vài phút — bug rò rỉ tài nguyên như đã gặp ở `KNOWN_ISSUES.md` mục 2 chỉ lộ ra sau thời gian dài).
2. Test đủ 3 kịch bản đã viết chi tiết trong `OTA_PLAN.md`: OTA thành công, App Rollback (firmware lỗi tự quay lại), firmware ký sai key bị từ chối.
3. Nếu có nhiều board C5 cùng lô, rollout theo canary (1-2 máy trước) — cần topic/kênh trigger có định danh riêng theo thiết bị (mục #4 trong `KNOWN_ISSUES.md`), **không** dùng chung 1 kênh broadcast cho toàn bộ fleet ngay từ lần đầu.
4. Chỉ sau khi canary ổn định, mới đưa vào firmware mặc định nạp tại nhà máy cho các lô sản xuất tiếp theo.

---

## 10. Checklist an toàn trước khi coi là "xong" (tổng hợp từ `KNOWN_ISSUES.md`, áp dụng lại)

- [ ] Kênh trigger OTA có xác thực (không dùng broker/API công khai ẩn danh).
- [ ] So sánh version dùng đúng phép so sánh "lớn hơn" (semver), không phải chỉ "khác nhau".
- [ ] Có device ID/định danh riêng trong kênh trigger nếu triển khai nhiều thiết bị.
- [ ] Logic khởi tạo lại kết nối mạng (nếu project cũ có sẵn) không tạo instance chồng chéo mỗi lần reconnect — kiểm tra kỹ nếu tái dùng kênh điều khiển sẵn có.
- [ ] Cơ chế reconnect ở tầng kết nối nền tảng (WiFi/kênh điều khiển) retry vô hạn có backoff, không bỏ cuộc vĩnh viễn sau N lần.
- [ ] Cert HTTPS (nếu dùng) có kế hoạch xoay vòng trước khi hết hạn, tránh kẹt vòng lặp "cần OTA để đổi cert nhưng cert cũ đã hết hạn".
- [ ] Đã đo free heap sau khi tích hợp OTA, còn đủ margin an toàn cho code nghiệp vụ cũ.
- [ ] Đã test đủ 3 kịch bản: OTA thành công, Rollback khi lỗi, từ chối firmware ký sai key.

---

## Tóm tắt thứ tự triển khai

```
0. Baseline (git tag, đo flash size, đo heap hiện tại)
        │
1. ✅ Thiết bị còn nạp lại được qua cáp (đã xác nhận) — điều kiện tiên quyết đã có sẵn
        │
2. Thiết kế + build custom partitions.csv, giữ nguyên NVS hiện có nếu được
        │
3. Bật App Rollback (menuconfig)
        │
4. Xác nhận đúng Secure Boot scheme cho C5 (KHÔNG giả định giống ESP32 gốc) + tạo khóa ký
        │
5. Viết components/ota_manager/ với API transport-agnostic, chỉ 3-4 điểm nối vào code cũ
        │
6. Áp tuning đã kiểm chứng (buffer_caps CÓ MALLOC_CAP_8BIT, watchdog timeout)
        │
7. Chọn kênh trigger tái dùng hạ tầng sẵn có (ưu tiên) hoặc thêm mới có auth
        │
8. Test bench bằng otatool.py (không cần mạng) trước
        │
9. Nạp 1 lần qua cáp lên board bench đầu tiên → chạy ổn định vài ngày → test đủ 3 kịch bản
        │
10. Rollout canary → checklist an toàn → mới áp dụng cho lô sản xuất tiếp theo
```
