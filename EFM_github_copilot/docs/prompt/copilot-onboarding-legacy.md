# Hướng dẫn "dạy" GitHub Copilot hiểu repo giai đoạn 1

Dùng khi **tiếp quản repo firmware đã phát triển** (giai đoạn 1) để thêm tính năng, refactor và tối ưu.
Làm tuần tự từ giai đoạn A đến G. Mỗi prompt đều copy dán được vào Copilot Chat trong VS Code.

## 0. Hiểu đúng trước khi bắt đầu

- **Copilot không nhớ gì giữa các phiên chat.** "Dạy" Copilot nghĩa là **ghi kiến thức vào file** mà Copilot
  tự nạp mỗi lần:
  - `.github/copilot-instructions.md`: nạp ở mọi request.
  - `.github/instructions/*.instructions.md`: nạp theo `applyTo`.
  - `docs/*.md`: Copilot đọc khi được link tới hoặc khi tìm thấy qua `#codebase`.

  Kiến thức chỉ nằm trong lịch sử chat thì sẽ mất khi mở chat mới.
- **Copilot có thể bịa.** Mọi prompt bên dưới đều yêu cầu dẫn chứng `file:line`. Tài liệu Copilot sinh ra
  phải được **người review** trước khi đưa vào instructions (giai đoạn B và C4).
- **3 tài liệu chính được điền từ code phase 1.** Trong bộ kit, `docs/architecture.md`, `docs/dataflow.md` và
  `docs/security.md` **chỉ có đề mục**. Quy trình như sau:
  - Giai đoạn A: ghi chú khảo sát thô vào `docs/phase1/`.
  - Giai đoạn C: agent **điền nội dung dưới từng đề mục** từ code phase 1. Giữ nguyên tên và thứ tự đề mục;
    được thêm đề mục con khi cần.

  Từ đó 3 file này là nguồn sự thật mà Copilot đọc ở mọi phiên sau.
- **Chọn mode phù hợp:**

  | Việc | Mode trong Chat |
  |---|---|
  | Khảo sát, hỏi đáp, không sửa file | **Ask**, hoặc agent **Planner** |
  | Ghi tài liệu khảo sát ra file | **Agent** (chỉ cho phép tạo/sửa file trong `docs/`) |
  | Lập kế hoạch refactor/tính năng | agent **Planner** |
  | Triển khai | **Agent** |

- **Tham chiếu ngữ cảnh trong prompt:**
  - `#codebase`: cho Copilot tìm kiếm trong toàn repo.
  - `#file:đường/dẫn`: đính kèm 1 file cụ thể.
  - `#selection`: đoạn code đang chọn.

  Nên đính kèm file cụ thể khi đã biết file cần đọc, để câu trả lời chính xác hơn.
- **Mỗi chủ đề mở 1 chat mới.** Chat quá dài làm Copilot "quên" phần đầu và dễ trộn thông tin.

---

## Giai đoạn A0 – Chuẩn bị repo (người làm, khoảng 30 phút)

1. Tạo branch riêng: `git switch -c chore/copilot-onboarding`.
2. Copy bộ kit vào root repo: `.github/`, `.vscode/`, `docs/`, `tools/`, `.clang-format`, `.clang-tidy`, `.clangd`,
   `.editorconfig`, `.gitmessage`. **Không ghi đè** file cùng tên đã có trong repo cũ mà chưa so sánh
   (nhất là `.gitignore`, `.vscode/settings.json`, `partitions.csv`).
3. `sh tools/install-hooks.sh`.
4. **Chụp baseline** trước khi đụng vào code, rồi ghi vào `docs/phase1/baseline.md`:
   ```bash
   mkdir -p docs/phase1
   idf.py set-target esp32c5 && idf.py build 2>&1 | tee docs/phase1/build-baseline.log
   idf.py size > docs/phase1/size-baseline.txt
   idf.py size-components >> docs/phase1/size-baseline.txt
   grep -c ': warning: ' docs/phase1/build-baseline.log
   git tag phase1-baseline
   ```
   Trên board thật, ghi lại: heap tối thiểu sau 1 giờ chạy, stack watermark từng task (nếu có lệnh CLI),
   dòng tiêu thụ, thời gian boot đến lúc kết nối xong.
5. **Chưa chạy clang-format lên code cũ.** Việc format để ở giai đoạn E, sau khi đã có test bảo vệ.
6. Tạm thời thêm đoạn sau vào cuối `.github/copilot-instructions.md`, gỡ ra ở bước D2:
   ```markdown
   ## Trạng thái hiện tại: ĐANG KHẢO SÁT repo giai đoạn 1
   - Code hiện có CHƯA tuân thủ các quy tắc ở trên. KHÔNG tự sửa/refactor code cũ khi không được yêu cầu.
   - Khi mô tả code: luôn dẫn chứng `file:line`; phân biệt rõ "đọc thấy trong code" và "suy đoán".
   - docs/architecture.md, docs/dataflow.md, docs/security.md đang chỉ có đề mục (chưa có nội dung).
   ```

---

## Giai đoạn A – Khảo sát hiện trạng (Copilot làm, người review)

Mỗi prompt chạy trong **1 chat mới**, ở mode **Agent**, để Copilot tự ghi kết quả ra file `docs/phase1/`.
Đây là **ghi chú thô** làm đầu vào cho giai đoạn C.

### A1. Tổng quan repo

```text
Khảo sát toàn bộ repo #codebase. CHỈ ĐỌC, không sửa code.
Tạo file docs/phase1/overview.md gồm:
1. Mục đích thiết bị theo những gì code thể hiện.
2. Cây thư mục (2 cấp) + 1 dòng mô tả mỗi thư mục/component.
3. Phiên bản ESP-IDF, target, toolchain (tìm trong CMakeLists.txt, sdkconfig.defaults, dependencies.lock, CI).
4. Danh sách component (components/ và managed_components/ khai báo trong idf_component.yml), vai trò, dependency.
5. Cách build/flash/monitor thực tế (lệnh, script có sẵn).
6. Những gì không rõ hoặc mâu thuẫn -> mục "Câu hỏi mở".
Mỗi khẳng định kèm dẫn chứng file:line. Không đoán; nếu không tìm thấy thì ghi "không tìm thấy".
```

### A2. Boot sequence, task và concurrency

```text
Đọc #file:main/main.c và mọi nơi gọi xTaskCreate*/esp_timer_create/esp_event_handler_register trong #codebase.
Tạo docs/phase1/runtime.md gồm:
1. Trình tự boot từ app_main() theo thứ tự thực thi (dẫn file:line từng bước).
2. Bảng task: tên, hàm, stack, priority, core, chờ trên gì (queue/semaphore/delay), có đăng ký TWDT không.
3. Bảng timer, ISR, event handler, callback (chạy trong context nào).
4. Tài nguyên dùng chung giữa các task (biến global/static, buffer) và cơ chế bảo vệ (mutex/không có).
5. Nghi vấn race condition/deadlock/block trong callback, có dẫn chứng code.
Không sửa code.
```

### A3. Từng component (lặp lại cho mỗi component)

```text
Phân tích component #file:components/<ten>/ (đọc tất cả file .c/.h, CMakeLists.txt, Kconfig).
Thêm 1 mục "## <ten>" vào docs/phase1/components.md gồm:
- Trách nhiệm (1-2 câu), API public (chữ ký + ý nghĩa), trạng thái nội bộ, task/callback riêng.
- Cách xử lý lỗi hiện tại: có kiểm tra esp_err_t không, có timeout không, có retry/backoff không.
- Bộ nhớ: malloc/free ở đâu, buffer cố định kích thước bao nhiêu, hàm chuỗi không giới hạn (strcpy/sprintf...).
- Những điểm vi phạm .github/instructions/c-esp-idf.instructions.md (liệt kê, CHƯA sửa).
- Mức độ tái sử dụng khi refactor/thêm tính năng: giữ nguyên / bọc lại / viết lại (kèm lý do).
Dẫn chứng file:line. Không sửa code.
```

### A4. Luồng dữ liệu

```text
Truy vết luồng dữ liệu trong #codebase, tạo docs/phase1/dataflow-current.md với sơ đồ mermaid:
1. Uplink: từ lúc đọc cảm biến -> đóng gói -> gửi qua WiFi/MQTT và/hoặc LoRa (hàm nào gọi hàm nào, buffer nào).
2. Downlink: từ lúc nhận MQTT message / AT "+EVT:RX" -> parse -> thực thi lệnh.
3. OTA: nhận lệnh -> tải -> kiểm tra -> reboot -> xác nhận/rollback.
4. Màn hình và nút bấm: sự kiện nút đi đâu, màn hình lấy dữ liệu từ đâu, ai gọi hàm vẽ.
5. Khi mất kết nối: dữ liệu bị bỏ, đệm lại hay chặn luồng?
Mỗi mũi tên trong sơ đồ phải tương ứng 1 lời gọi hàm có thật (ghi file:line ở bảng bên dưới sơ đồ).
```

### A5. Giao tiếp bên ngoài (hợp đồng với server/phần cứng – KHÔNG được phá vỡ)

```text
Tìm trong #codebase và tạo docs/phase1/interfaces.md:
1. MQTT: broker URL/port, TLS hay không, cert ở đâu, client id, LWT, danh sách topic (pub/sub, QoS, retain),
   định dạng payload của từng topic (ví dụ JSON thực tế từ code).
2. LoRaWAN: region/band, class, OTAA/ABP, fPort, định dạng payload nhị phân (byte-by-byte), confirmed/unconfirmed.
3. Tất cả lệnh AT gửi tới RAK3172 (lệnh, timeout, cách xử lý phản hồi lỗi) và các +EVT được xử lý.
4. OTA: URL, định dạng lệnh, cách kiểm tra image.
5. CLI/console: danh sách lệnh.
Đây là "hợp đồng" với server đang chạy: đánh dấu rõ mục nào server phụ thuộc.
```

### A6. Cấu hình, lưu trữ, phần cứng

```text
Tạo docs/phase1/config-hw.md từ #codebase:
1. partitions.csv hiện tại (bảng), flash size cấu hình.
2. NVS: namespace, key, kiểu dữ liệu, struct được lưu (layout, có version/crc không), khi nào ghi.
3. Kconfig của dự án và các giá trị quan trọng trong sdkconfig.defaults (bảo mật, WDT, log, rollback).
4. Pin map: mọi GPIO/UART/SPI/I2C đang dùng (chân, chức năng, file:line). Phát hiện xung đột chân nếu có.
5. Tính năng bảo mật đang bật/tắt: Secure Boot, Flash Encryption, NVS encryption, TLS verify.
```

### A7. Nợ kỹ thuật và rủi ro

```text
Dựa trên #codebase và các file đã tạo trong docs/phase1/, tạo docs/phase1/risks.md:
Liệt kê vấn đề theo mức độ BLOCKER/MAJOR/MINOR (định nghĩa trong .github/instructions/review.instructions.md),
mỗi mục gồm: file:line, kịch bản lỗi cụ thể (input/trạng thái -> hậu quả), ảnh hưởng khi chạy dài hạn.
Ưu tiên tìm: tràn buffer, thiếu timeout, block trong callback/ISR, rò rỉ bộ nhớ, retry không backoff,
ghi NVS liên tục, secret hard-code/log ra, TLS không verify, OTA không rollback, tràn tick counter.
Chỉ ghi vấn đề có bằng chứng trong code. Không sửa code.
```

> Mẹo: ở bước A7 nên chạy thêm `/review-code <thư mục>` cho từng component quan trọng
> (`rak3172`, transport, OTA), rồi gộp kết quả vào `risks.md`.

---

## Giai đoạn B – Kiểm chứng ghi chú khảo sát (người làm, bắt buộc)

Tài liệu sai còn tệ hơn không có tài liệu, vì Copilot sẽ tin tài liệu đó ở mọi phiên sau.

1. Cho Copilot tự kiểm tra lại từng file trong `docs/phase1/`, mỗi file trong **1 chat mới**:
   ```text
   Kiểm tra từng khẳng định trong #file:docs/phase1/runtime.md với code thực tế trong #codebase.
   Với mỗi khẳng định: ĐÚNG / SAI / KHÔNG KIỂM CHỨNG ĐƯỢC + dẫn chứng file:line.
   Sửa trực tiếp các chỗ SAI trong file tài liệu, đánh dấu "(chưa kiểm chứng)" cho chỗ không kiểm chứng được.
   ```
2. Người phụ trách đọc lại, đối chiếu thêm với board thật: log boot, thử lệnh CLI, bắt gói MQTT bằng `mosquitto_sub`,
   xem uplink trên network server.
3. Hỏi lại team giai đoạn 1 (nếu còn liên lạc được) về các mục "Câu hỏi mở".
4. Commit: `docs: add phase1 current-state analysis`.

---

## Giai đoạn C – Điền `architecture.md`, `dataflow.md`, `security.md` từ code phase 1

Đầu vào, theo thứ tự tin cậy:
1. **Code thật**: `main/`, `components/`, `CMakeLists.txt`, `Kconfig*`, `sdkconfig.defaults*`, `partitions.csv`,
   `idf_component.yml`.
2. **Comment trong code và lịch sử git**: dùng để hiểu *vì sao* code được thiết kế như vậy.
3. **Ghi chú đã kiểm chứng** trong `docs/phase1/`.

3 file đã có sẵn đề mục. Agent **điền nội dung dưới từng đề mục**, giữ nguyên tên và thứ tự đề mục
(được thêm đề mục con khi cần). Đề mục nào code không có tính năng tương ứng thì ghi rõ
"Không có trong code phase 1", **không xoá đề mục**.

Mỗi file viết trong **1 chat mới**, mode **Agent**, theo thứ tự C1 → C2 → C3. Dataflow và security dùng tên
component lấy từ architecture, nên phải viết architecture trước.

> Nếu repo lớn: chạy prompt theo từng mục (ví dụ "chỉ viết mục 1–5"), rồi "viết tiếp mục 6–13" trong cùng chat.
> Làm vậy để Copilot đọc đủ code cho từng mục thay vì tóm tắt hời hợt.

### C1. `docs/architecture.md` – kiến trúc hiện tại

```text
Điền nội dung cho #file:docs/architecture.md (hiện chỉ có đề mục), mô tả kiến trúc HIỆN TẠI của firmware
phase 1, học từ code thật. Giữ nguyên tên và thứ tự đề mục; được thêm đề mục con; đề mục nào code không có
thì ghi "Không có trong code phase 1", không xoá đề mục.

Nguồn (theo thứ tự tin cậy):
1) Code trong #codebase: main/, components/, CMakeLists.txt, Kconfig*, sdkconfig.defaults*, partitions.csv, idf_component.yml.
2) Comment trong code và lịch sử git (chạy `git log --stat --follow <file>` trong terminal) để hiểu lý do thiết kế.
3) Ghi chú đã kiểm chứng: #file:docs/phase1/overview.md #file:docs/phase1/runtime.md
   #file:docs/phase1/components.md #file:docs/phase1/config-hw.md #file:docs/phase1/size-baseline.txt

Quy tắc:
- Mỗi khẳng định kỹ thuật kèm dẫn chứng `file:line` (hoặc commit hash).
- Số liệu (stack, priority, kích thước buffer/queue, timeout, partition) lấy NGUYÊN giá trị trong code/Kconfig,
  ghi rõ macro/Kconfig nguồn. Không làm tròn, không ước lượng.
- Điều không đọc trực tiếp được từ code thì đánh dấu "(suy luận)". Không tìm thấy thì ghi "không tìm thấy trong code".
- Chỉ mô tả, KHÔNG sửa code, KHÔNG đề xuất thiết kế mới.

Nội dung cần điền cho từng đề mục (số thứ tự trùng với số đề mục trong file):
1. Tổng quan: chức năng thiết bị, phần cứng (MCU, RAK3172, màn hình, nút, cảm biến – model nếu code thể hiện),
   ESP-IDF version, target, version firmware (version.txt / PROJECT_VER).
2. Sơ đồ phân lớp và phụ thuộc component (mermaid flowchart), dựng từ REQUIRES/PRIV_REQUIRES trong CMakeLists.txt
   và #include thực tế. Đánh dấu phụ thuộc vòng, hoặc lớp dưới gọi ngược lớp trên.
3. Bảng component: tên, trách nhiệm, API public chính, file chính, phụ thuộc.
4. Cơ chế/pattern đang dùng (interface bằng con trỏ hàm, bảng dispatch, state machine, callback, event, queue...):
   chỉ những gì có trong code, kèm vị trí.
5. Bảng task/timer/ISR: tên, hàm, stack, priority, core, chờ trên gì, có đăng ký Task WDT không.
6. State machine có trong code: enum trạng thái + điều kiện chuyển (mermaid stateDiagram),
   mỗi chuyển trạng thái kèm file:line. Nếu trạng thái được quản lý bằng nhiều cờ bool rải rác, mô tả đúng như vậy.
7. Trình tự boot từ app_main() theo đúng thứ tự thực thi, kèm hành vi khi từng bước lỗi.
8. Cấu hình: Kconfig của dự án, giá trị quan trọng trong sdkconfig.defaults, NVS (namespace/key/struct/version/crc).
9. Partition và bộ nhớ: bảng partitions.csv, kích thước app, phần trăm còn trống của slot app/OTA.
10. Quyết định thiết kế đã thấy (từ code/comment/git log) và lý do nếu tìm được.
11. Điểm lệch so với .github/instructions/c-esp-idf.instructions.md (tóm tắt, link docs/phase1/risks.md).
12. Câu hỏi mở.
13. Cấu trúc thư mục (2 cấp).

Viết tiếng Việt, ngắn gọn, ưu tiên bảng. Khi xong, liệt kê trong câu trả lời (không ghi vào file) mọi chỗ
"(suy luận)" hoặc "không tìm thấy trong code" để người review xác nhận.
```

### C2. `docs/dataflow.md` – luồng dữ liệu hiện tại

```text
Điền nội dung cho #file:docs/dataflow.md (hiện chỉ có đề mục), mô tả luồng dữ liệu HIỆN TẠI của firmware
phase 1, học từ code thật. Giữ nguyên tên và thứ tự đề mục; được thêm đề mục con; đề mục nào code không có
thì ghi "Không có trong code phase 1", không xoá đề mục.

Nguồn: code trong #codebase; ghi chú đã kiểm chứng #file:docs/phase1/dataflow-current.md
#file:docs/phase1/interfaces.md; #file:docs/architecture.md (vừa điền, dùng đúng tên component/task).

Quy tắc:
- Mỗi khẳng định kèm dẫn chứng file:line. Suy luận đánh dấu "(suy luận)". Không tìm thấy ghi "không tìm thấy trong code".
- Mỗi mũi tên trong sơ đồ mermaid phải là 1 lời gọi hàm / queue / event / callback có thật.
  Dưới mỗi sơ đồ có bảng "bước | hàm | context (task/ISR/callback) | file:line".
- Topic MQTT, fPort, lệnh AT, key JSON, tên NVS key: ghi NGUYÊN VĂN chuỗi trong code.
- Payload nhị phân: dựng bố cục từ code encode/decode thật (offset, kiểu, endian, hệ số scale).
- Chỉ mô tả, KHÔNG sửa code, KHÔNG đề xuất định dạng mới.

Nội dung cần điền cho từng đề mục (số thứ tự trùng với số đề mục trong file):
1. Sơ đồ tổng quan (mermaid flowchart): cảm biến, nút, màn hình, transport, server, OTA, cấu hình.
2. Uplink: đọc cảm biến -> xử lý -> đóng gói -> gửi (WiFi/MQTT và LoRa) (mermaid sequenceDiagram);
   ai sở hữu buffer, chạy trong task nào, chu kỳ gửi lấy từ đâu.
3. Downlink: MQTT message / AT "+EVT:RX..." -> parse -> validate (nếu có) -> thực thi -> phản hồi (nếu có).
4. OTA: nhận lệnh -> tải -> kiểm tra -> ghi -> reboot -> xác nhận/rollback.
5. Nút bấm: ngắt/polling -> debounce (ở đâu, bao nhiêu ms) -> sự kiện đi đâu -> hành động.
6. Màn hình: ai gọi hàm vẽ, dữ liệu lấy từ đâu, tần suất cập nhật, chạy trong task nào.
7. Cấu hình: CLI / UI / downlink -> validate -> ghi NVS -> áp dụng ngay hay sau reboot.
8. Mất kết nối và khôi phục: dữ liệu bị bỏ / đệm / chặn luồng; reconnect và backoff thực hiện thế nào.
9. Định dạng bản tin – HỢP ĐỒNG với server (ghi rõ: đổi mục này là BREAKING CHANGE):
   9.1 MQTT: broker/port/TLS, client id, LWT; bảng topic (chuỗi nguyên văn, pub/sub, QoS, retain);
       JSON mẫu cho từng topic dựng từ code encode.
   9.2 LoRaWAN: region/band, class, OTAA/ABP, bảng fPort, bố cục payload từng byte, confirmed/unconfirmed.
   9.3 Lệnh AT dùng với RAK3172 (lệnh, timeout, xử lý lỗi) và các "+EVT" được xử lý.
10. Bảng queue/buffer: tên, độ dài, kích thước phần tử, ai ghi / ai đọc, hành vi khi đầy.
11. Edge case chưa được xử lý trong các luồng (tóm tắt, link docs/phase1/risks.md).
12. Câu hỏi mở.

Khi xong, liệt kê trong câu trả lời mọi chỗ "(suy luận)" hoặc "không tìm thấy trong code".
```

### C3. `docs/security.md` – tình trạng bảo mật hiện tại

**Bước người làm trước** (Copilot không được chạy lệnh eFuse): trên 1 board đang chạy firmware phase 1,
đọc trạng thái eFuse. Lệnh này **chỉ đọc**:

```bash
idf.py -p /dev/ttyACM0 efuse-summary > docs/phase1/efuse-summary.txt
```

Xem lại file trước khi commit (có MAC và ID chip). Nếu không có board thì bỏ qua; khi đó Copilot sẽ ghi
"chưa xác định" cho các mục phụ thuộc eFuse.

```text
Điền nội dung cho #file:docs/security.md (hiện chỉ có đề mục), mô tả tình trạng bảo mật HIỆN TẠI của firmware
phase 1, học từ code và cấu hình thật. Giữ nguyên tên và thứ tự đề mục; được thêm đề mục con; đề mục nào
không áp dụng thì ghi "Không có trong code phase 1", không xoá đề mục.

Nguồn: code trong #codebase; sdkconfig.defaults*; build/config/sdkconfig.h (giá trị cấu hình THỰC TẾ sau khi build);
partitions.csv; các file cert/key nhúng (EMBED_TXTFILES/EMBED_FILES trong CMakeLists.txt);
#file:docs/phase1/config-hw.md #file:docs/phase1/interfaces.md #file:docs/phase1/risks.md;
docs/phase1/efuse-summary.txt nếu có; #file:docs/architecture.md #file:docs/dataflow.md (dùng đúng tên).
KHÔNG chạy espefuse.py, espsecure.py hay bất kỳ lệnh ghi flash nào.

Quy tắc:
- Mỗi khẳng định kèm dẫn chứng file:line (hoặc tên option sdkconfig + giá trị).
  Suy luận đánh dấu "(suy luận)". Không xác định được ghi "chưa xác định".
- KHÔNG chép secret thật (mật khẩu, key, token, private key) vào tài liệu:
  chỉ ghi vị trí file:line và thay giá trị bằng "<REDACTED>".
- Chỉ mô tả hiện trạng, KHÔNG sửa code, KHÔNG đề xuất bật eFuse.

Nội dung cần điền cho từng đề mục (số thứ tự trùng với số đề mục trong file):
1. Tài sản cần bảo vệ: WiFi/MQTT credential, LoRaWAN key (DevEUI/AppEUI/AppKey), cert/key TLS, khoá ký, firmware.
   Mỗi loại: lưu ở đâu (NVS key / hard-code / file nhúng / trong RAK3172), có mã hoá không,
   có bị log ra hoặc in ra CLI không (file:line).
2. Bảng cấu hình bảo mật với giá trị THỰC TẾ và nguồn: Secure Boot (version, scheme), Flash Encryption (mode),
   NVS encryption, app rollback, anti-rollback/secure_version, JTAG/USB-JTAG, ROM download mode,
   log level mặc định, Task WDT/panic behavior, core dump.
3. Kết nối mạng: MQTT (mqtt:// hay mqtts://, CA cert nhúng ở đâu, có verify hostname không,
   có skip_cert_common_name_check không, client cert, username/password); HTTPS OTA (verify cert?); SNTP nếu có.
4. OTA: lệnh đến từ đâu, có xác thực lệnh không, kiểm tra image thế nào (chữ ký / sha256 / version),
   rollback, chống hạ cấp version.
5. Điểm nhận dữ liệu từ ngoài: MQTT downlink, LoRa downlink, phản hồi AT, CLI, dữ liệu NVS, nút bấm.
   Bảng: điểm nhận | hàm xử lý | có validate độ dài/phạm vi/định dạng không | file:line.
6. Giao diện debug/bảo trì: console/CLI (lệnh nào nguy hiểm, có xác thực không, có bị tắt ở release không), JTAG.
7. Mô hình đe doạ áp dụng cho HIỆN TRẠNG. Bảng: mối đe doạ | biện pháp đang có (file:line) | khoảng trống |
   mức độ BLOCKER/MAJOR/MINOR (theo .github/instructions/review.instructions.md).
8. Checklist production với trạng thái hiện tại: ☑ đã có / ☐ chưa có / ? chưa xác định.
9. Câu hỏi mở.

Khi xong, liệt kê trong câu trả lời mọi chỗ "(suy luận)" hoặc "chưa xác định".
```

### C4. Kiểm chứng 3 tài liệu (bắt buộc)

1. **Copilot tự kiểm tra từng file**, mỗi file trong 1 chat mới:
   ```text
   Kiểm tra từng khẳng định trong #file:docs/architecture.md với code thực tế trong #codebase.
   Với mỗi khẳng định: ĐÚNG / SAI / KHÔNG KIỂM CHỨNG ĐƯỢC + dẫn chứng file:line.
   Kiểm tra mọi dẫn chứng file:line trong tài liệu còn trỏ đúng dòng code được nói tới.
   Sửa trực tiếp chỗ SAI; chỗ không kiểm chứng được đánh dấu "(chưa kiểm chứng)". Hiển thị diff.
   ```
   Lặp lại với `docs/dataflow.md` và `docs/security.md`.
2. **Đối chiếu chéo 3 file** (chat mới):
   ```text
   Đối chiếu chéo #file:docs/architecture.md #file:docs/dataflow.md #file:docs/security.md với nhau và với #codebase:
   tên component, tên task, số liệu (stack/priority/queue/timeout), topic, fPort, NVS key, partition
   phải thống nhất giữa 3 file và đúng với code.
   Liệt kê mọi mâu thuẫn (vị trí trong tài liệu + file:line trong code) và sửa theo code. Hiển thị diff.
   ```
3. **Kiểm tra đề mục còn trống** (chat mới):
   ```text
   Trong #file:docs/architecture.md #file:docs/dataflow.md #file:docs/security.md, liệt kê đề mục nào còn trống,
   quá sơ sài, hoặc ghi "Không có trong code phase 1".
   Với mỗi đề mục: tìm lại trong #codebase để xác nhận code thật sự không có tính năng đó (file:line nơi đã tìm),
   hay trước đó chưa đọc đủ code. Nếu tìm thấy thì điền bổ sung. Hiển thị diff.
   ```
4. **Người review:**
   - Tìm các chỗ cần xác nhận:
     `grep -n "(suy luận)\|không tìm thấy\|chưa xác định\|chưa kiểm chứng" docs/architecture.md docs/dataflow.md docs/security.md`
   - Xác nhận với code, board hoặc team phase 1.
   - Mở vài dẫn chứng `file:line` ngẫu nhiên để kiểm tra.
   - Xem sơ đồ mermaid hiển thị đúng. Kiểm tra trên GitHub, hoặc cài extension *Markdown Preview Mermaid Support*
     cho VS Code.
5. Commit: `docs: fill architecture, dataflow, security from phase1 code`.

---

## Giai đoạn D – Đưa kiến thức vào instructions (Copilot đề xuất, người duyệt)

### D1. Phân tích khoảng cách giữa hiện trạng và quy tắc chất lượng

```text
So sánh hiện trạng (#file:docs/architecture.md #file:docs/dataflow.md #file:docs/security.md #file:docs/phase1/risks.md)
với các quy tắc của team (#file:.github/copilot-instructions.md mục "Quy tắc kiến trúc bắt buộc",
#file:.github/instructions/c-esp-idf.instructions.md, mục 8 "Checklist production" trong docs/security.md).
Tạo docs/phase1/gap-analysis.md: bảng từng quy tắc/khía cạnh (phân lớp, interface, xử lý lỗi, bộ nhớ, concurrency,
độ bền dài hạn, bảo mật, khả năng test) -> [đạt | đạt một phần | chưa đạt], kèm component liên quan, file:line,
rủi ro, và hướng xử lý đề xuất (giữ nguyên / bọc lại / refactor / viết lại).
Chỉ ra quy tắc nào KHÔNG phù hợp với repo này (nếu có) để team cân nhắc sửa quy tắc.
```

### D2. Cập nhật `copilot-instructions.md`, skills và agent theo repo thật

```text
Đề xuất chỉnh #file:.github/copilot-instructions.md cho khớp repo này, dựa trên
#file:docs/architecture.md #file:docs/dataflow.md #file:docs/security.md:
- Cập nhật mục "Sản phẩm", "Cấu trúc repo", "Quy tắc kiến trúc bắt buộc" và "Lệnh build & kiểm tra" theo thực tế.
  Quy tắc nào nhắc tới thứ chưa có trong code (vd transport_if_t) thì chuyển thành "mục tiêu, áp dụng cho code mới".
- Thêm mục "Hợp đồng không được phá vỡ": topic/payload MQTT, payload LoRa, NVS layout, partition
  (tóm tắt 1 dòng mỗi mục + link tới mục 9 của docs/dataflow.md).
- Thêm mục "Code kế thừa": các thư mục/file chưa tuân thủ quy tắc, và quy định "chỉ refactor khi task yêu cầu".
- Giữ file dưới 2 trang; chi tiết để trong docs/ và link tới.
Sau đó kiểm tra .github/skills/*/SKILL.md và .github/agents/*.agent.md: liệt kê chỗ nhắc tới file/component/hàm
không tồn tại trong repo này, đề xuất sửa cho khớp.
Hiển thị tất cả dạng diff, CHƯA ghi file.
```

Người duyệt diff rồi mới cho ghi. **Gỡ** mục "ĐANG KHẢO SÁT" đã thêm tạm ở bước A0.6.

### D3. Instructions riêng cho vùng code cũ (tuỳ chọn)

Nếu có vùng code cũ lớn chưa refactor ngay, tạo `.github/instructions/legacy.instructions.md`:

```markdown
---
name: 'Legacy code rules'
description: 'Quy tắc khi sửa code kế thừa giai đoạn 1 chưa refactor.'
applyTo: 'main/legacy/**,components/old_*/**'
---
- Code này CHƯA tuân thủ c-esp-idf.instructions.md. Khi sửa bug: sửa tối thiểu, không refactor kèm.
- Không đổi chữ ký hàm public, topic, payload, NVS key.
- Mọi thay đổi hành vi phải có characterization test (test/host/) trước.
```

(Thay `applyTo` bằng đường dẫn thật của repo.)

### D4. Kiểm tra Copilot đã "hiểu" chưa

Mở **chat mới**, hỏi các câu dưới đây **không đính kèm file**, rồi kiểm tra mục *References* xem Copilot
đã nạp đúng instructions chưa:

```text
1. Khi thiết bị mất WiFi 10 phút, dữ liệu cảm biến trong thời gian đó đi đâu?
2. Thêm 1 trường "battery_mv" vào bản tin cảm biến thì phải sửa những file nào, và có phá vỡ server không?
3. Task nào có thể bị block lâu nhất và hậu quả là gì?
4. Hiện tại firmware có kiểm tra chữ ký image OTA không?
```

Trả lời sai hoặc chung chung nghĩa là tài liệu hoặc instructions còn thiếu. Bổ sung, rồi hỏi lại trong chat mới.

---

## Giai đoạn E – Lưới an toàn trước khi refactor

### E1. Characterization test (khoá hành vi hiện tại)

```text
Với các hàm logic thuần trong #file:components/<ten>/<file>.c (parser AT, encode/decode payload, validate config),
viết host test Unity trong test/host/ theo #file:.github/instructions/testing.instructions.md.
Mục tiêu: GHI LẠI HÀNH VI HIỆN TẠI (kể cả hành vi có vẻ sai – đánh dấu bằng comment "CURRENT BEHAVIOR, see risks.md"),
KHÔNG sửa code nguồn. Nếu hàm không test được vì phụ thuộc phần cứng, liệt kê và đề xuất cách tách tối thiểu.
```

Ưu tiên: encode/decode payload (hợp đồng với server, mục 9 của `docs/dataflow.md`), parser AT, load/save config.

### E2. Format toàn bộ code cũ (1 commit riêng)

```bash
clang-format -i $(git ls-files '*.c' '*.h' ':!:managed_components/**')
idf.py build                    # phải build giống hệt, 0 thay đổi hành vi
git commit -am "style: apply clang-format to phase1 code"
git rev-parse HEAD >> .git-blame-ignore-revs && git add .git-blame-ignore-revs
git commit -m "chore: ignore formatting commit in git blame"
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

Commit format riêng để các diff refactor sau này sạch, và `git blame` vẫn chỉ đúng tác giả gốc.
Sau bước này các dẫn chứng `file:line` trong docs có thể lệch dòng. Chạy lại prompt số 1 của bước C4 để cập nhật.

### E3. Bật công cụ phân tích

```bash
idf.py clang-check --exclude-paths managed_components
```

```text
Đọc warnings.txt (kết quả clang-tidy), gom nhóm theo loại và component, đối chiếu với docs/phase1/risks.md.
Bổ sung vào risks.md các cảnh báo thực sự là lỗi (bỏ qua cảnh báo style). Không sửa code.
```

---

## Giai đoạn F – Lập backlog (agent **Planner**)

```text
Dựa trên #file:docs/phase1/gap-analysis.md #file:docs/phase1/risks.md #file:docs/security.md
và danh sách yêu cầu mới dưới đây, lập backlog trong docs/phase1/backlog.md.

Yêu cầu mới:
- <tính năng 1>
- <tính năng 2>
- <mục tiêu tối ưu: ví dụ giảm dòng tiêu thụ, giảm thời gian kết nối, giảm kích thước firmware>

Mỗi mục backlog gồm: loại (fix/refactor/feat/perf/sec), mô tả, component ảnh hưởng, phụ thuộc mục nào,
rủi ro phá vỡ hợp đồng (mục 9 của docs/dataflow.md), cách kiểm chứng, ước lượng S/M/L.
Sắp xếp: BLOCKER an toàn/bảo mật -> refactor mở đường cho tính năng -> tính năng -> tối ưu.
Mỗi mục đủ nhỏ để làm trong 1 PR. Đánh dấu mục nào cần ADR.
```

Nguyên tắc sắp xếp:

- **Sửa lỗi nguy hiểm trước**, refactor sau, tính năng mới sau cùng.
- **Refactor theo kiểu "strangler":** bọc code cũ sau interface mới, ví dụ `transport_if_t`. Sau đó chuyển nơi gọi
  sang interface từng bước, rồi mới xoá code cũ. Không viết lại toàn bộ trong 1 lần.
- **Tối ưu phải đo trước và sau** so với baseline A0.4. Không tối ưu khi chưa đo.

---

## Giai đoạn G – Vòng lặp cho mỗi mục backlog

```
Planner (kế hoạch) -> người duyệt -> Agent (triển khai từng bước, build sau mỗi bước)
  -> host test -> /review-code -> test board -> /commit -> PR (template) -> cập nhật docs
```

### G1. Lập kế hoạch 1 mục (agent Planner)

```text
Lập kế hoạch cho mục backlog "<tên mục>" trong #file:docs/phase1/backlog.md.
Đọc code liên quan và #file:docs/architecture.md trước. Nêu rõ: các hàm/file sẽ đổi, hợp đồng nào có thể bị ảnh hưởng
(mục 9 của #file:docs/dataflow.md), test nào bảo vệ, các bước nhỏ (mỗi bước build được), commit tương ứng.
```

### G2. Refactor (Agent)

```text
Thực hiện bước <N> của kế hoạch trên. Đây là REFACTOR: không thay đổi hành vi quan sát được
(payload, topic, timing, NVS). Sau khi sửa: chạy idf.py build và host test, báo kết quả.
Không sửa code ngoài phạm vi bước này; nếu thấy vấn đề khác, ghi vào cuối câu trả lời thay vì sửa.
```

### G3. Tính năng mới (Agent)

```text
Triển khai bước <N>. Tuân theo pattern đang dùng trong #file:docs/architecture.md; phần thiết kế mới theo
quy tắc trong .github/copilot-instructions.md và các ADR đã duyệt trong docs/adr/.
Nếu cần component mới: dùng /new-component. Nếu thêm bản tin/lệnh: dùng /add-message-type.
Liệt kê edge case đã xử lý và những gì chưa kiểm chứng trên phần cứng.
```

### G4. Tối ưu (Agent)

```text
Mục tiêu tối ưu: <chỉ số, ví dụ heap tối thiểu / kích thước binary / dòng ngủ>. Baseline: <số liệu từ baseline.md>.
Đề xuất tối đa 3 thay đổi có tác động lớn nhất, ước lượng lợi ích và rủi ro từng cái, CHƯA sửa code.
```

Sau khi chọn: triển khai từng thay đổi, đo lại, ghi số liệu trước/sau vào commit (`Tested: bench - ...`).

### G5. Kết thúc mỗi mục

```text
Dựa trên các thay đổi trong branch này so với main, cập nhật #file:docs/architecture.md #file:docs/dataflow.md
#file:docs/security.md cho khớp code mới (giữ quy tắc: giữ nguyên đề mục, mọi khẳng định có file:line,
chỉ mô tả những gì đã có trong code). Đánh dấu mục backlog đã xong. Hiển thị diff tài liệu.
```

---

## Duy trì kiến thức cho Copilot

- **Sửa code thì sửa docs trong cùng PR.** Checklist trong PR template đã có mục này.
- **Khi Copilot lặp lại cùng 1 sai lầm**, thêm 1 quy tắc ngắn kèm lý do vào file instructions phù hợp. Không nhắc đi
  nhắc lại trong chat.
- **Mỗi tháng (hoặc sau mỗi mốc lớn)**, chạy prompt sau trong chat mới:
  ```text
  Kiểm tra .github/copilot-instructions.md, .github/instructions/*.md, .github/skills/*/SKILL.md,
  docs/architecture.md, docs/dataflow.md, docs/security.md có còn đúng với #codebase không.
  Liệt kê chỗ lỗi thời/mâu thuẫn và dẫn chứng file:line bị lệch, đề xuất diff. Chưa ghi file.
  ```
- **Khi đã refactor xong code kế thừa:** xoá `legacy.instructions.md` và mục "Code kế thừa" trong
  `copilot-instructions.md`. Có thể xoá `docs/phase1/`, giữ lại `baseline.md` và `backlog.md` làm lịch sử.

## Mẹo viết prompt hiệu quả

| Nên | Tránh |
|---|---|
| Nêu rõ **CHỈ ĐỌC** hoặc **được sửa file nào** | "Xem và cải thiện code" (Copilot sẽ sửa lan man) |
| Đính kèm `#file:` cụ thể khi đã biết | Chỉ dùng `#codebase` cho câu hỏi hẹp |
| Yêu cầu dẫn chứng `file:line` | Tin câu trả lời không có dẫn chứng |
| Yêu cầu hiển thị diff trước khi ghi | Để agent ghi thẳng file quan trọng (instructions, partitions, Kconfig) |
| 1 chủ đề / 1 chat | Chat kéo dài qua nhiều chủ đề |
| Yêu cầu nói "không chắc" khi không chắc | Câu hỏi dẫn dắt ("có phải X đúng không?") |
| Chia việc lớn thành bước nhỏ, build sau mỗi bước | "Refactor toàn bộ component transport" trong 1 prompt |

## Checklist tổng

- [ ] A0 – Branch, copy kit, cài hooks, baseline, tag `phase1-baseline`
- [ ] A1–A7 – Ghi chú thô `docs/phase1/`: overview, runtime, components, dataflow-current, interfaces, config-hw, risks
- [ ] B – Kiểm chứng ghi chú (Copilot tự kiểm tra + người review + đối chiếu board)
- [ ] C – Điền `docs/architecture.md`, `docs/dataflow.md`, `docs/security.md` theo đề mục từ code phase 1 + kiểm chứng C4
- [ ] D – gap-analysis, cập nhật `copilot-instructions.md`/skills/agent, `legacy.instructions.md` (nếu cần), kiểm tra Copilot hiểu đúng
- [ ] E – Characterization test, commit format riêng + `.git-blame-ignore-revs`, clang-tidy
- [ ] F – `docs/phase1/backlog.md` đã duyệt, ADR cho quyết định lớn
- [ ] G – Mỗi mục backlog: Planner → Agent → test → `/review-code` → board → `/commit` → PR → docs
