# Chi tiết & khắc phục sự cố

Tài liệu này giải thích **vì sao** và **hạn chế đã kiểm chứng** của các mục trong
[README.md](../README.md). README chỉ liệt kê việc phải làm; chi tiết/lý do nằm ở đây.

## 4 công cụ kiểm tra code, không thay thế nhau

| File/hook | Trả lời câu hỏi | Chạy khi nào |
|---|---|---|
| `.clang-format` | Code có đúng hình thức không? (thụt lề, xuống dòng, khoảng trắng – **không** xem logic) | Lưu file (`editor.formatOnSave`), hoặc chạy tay |
| `.clang-tidy` | Code có lỗi logic/an toàn không? (buffer, con trỏ, quên kiểm tra lỗi, đặt tên sai quy ước...) | Chạy tay (`idf.py clang-check`), hoặc ngầm qua clangd khi gõ code |
| `.clangd` | Editor hiểu code thế nào để gợi ý, nhảy tới định nghĩa, gạch chân lỗi? | Suốt lúc mở VS Code; cần `build/compile_commands.json` có trước |
| `tools/hooks/pre-commit` | File `.c`/`.h` đang stage có đúng `.clang-format` không? | Tự động khi `git commit`, chạy **trước** `commit-msg`, sau khi đã chạy `tools/install-hooks.sh` |
| `tools/hooks/commit-msg` | Commit message có đúng format của team không? | Tự động khi `git commit`, sau khi đã chạy `tools/install-hooks.sh` |

## clangd dùng nhầm bản

**Đã kiểm chứng thật:** clangd chỉ phân tích đúng khi dùng **đúng bản `clangd` của `esp-clang`**
(có hỗ trợ kiến trúc Xtensa thật – clangd gốc/bản extension tự tải **không** hỗ trợ Xtensa). Dùng nhầm bản
→ báo sai `'stdio.h' file not found` (hoặc header lạ khác) cho **toàn bộ** file, dù code không có lỗi gì.
Test cụ thể: cùng 1 file, cùng cấu hình `--query-driver`, chạy bằng clangd hệ thống (vanilla) → báo thiếu
header không liên quan; chạy bằng đúng clangd của `esp-clang` → parse thành công, chỉ còn cảnh báo thật.

- Kiểm tra: mở terminal ESP-IDF, gõ `which clangd` – phải trỏ vào `.../esp-clang/.../bin/clangd`,
  không phải `/usr/bin/clangd` hay đường dẫn khác.
- Nếu extension clangd của VS Code không tự thấy bản đó (ví dụ VS Code được mở từ icon desktop,
  không kế thừa PATH của terminal), đặt trong **User Settings** (không đặt trong `.vscode/settings.json`
  của repo vì đường dẫn có số phiên bản khác nhau tuỳ máy/tuỳ thời điểm cài):
  ```json
  "clangd.path": "/home/<user>/.espressif/tools/esp-clang/<phien-ban>/esp-clang/bin/clangd"
  ```
  Lấy đúng giá trị từ `which clangd` ở bước trên.

`.vscode/settings.json` đặt `--query-driver=**/riscv32-esp-elf-*,**/xtensa-esp32*-elf-*` để khớp cả 2
kiến trúc (RISC-V cho ESP32-C5, Xtensa cho board test khác) – thiếu glob của kiến trúc nào thì header
chuẩn (`stdio.h`...) của kiến trúc đó báo "file not found" toàn bộ, không phân tích được gì.

## Terminal ESP-IDF

`.vscode/settings.json` khai báo sẵn 1 terminal profile tên **ESP-IDF** làm mặc định, tự nạp
`tools/idf-env.sh` (nạp `~/.bashrc` rồi `$IDF_PATH/export.sh`).

**Chưa biết `IDF_PATH` của máy mình ở đâu?**
```bash
type get_idf                                                        # cach 1: neu tung cai theo huong dan Espressif
find ~ -maxdepth 4 -iname export.sh 2>/dev/null | grep -v /build/   # cach 2: tim thu muc co export.sh + tools/idf.py
```
Thư mục chứa `export.sh` tìm được (có `tools/idf.py` bên trong) chính là giá trị cần đặt cho `IDF_PATH`.

**Không thấy dòng `[idf-env] ESP-IDF v5.5.x ready` khi mở terminal mới:**
1. Đã mở đúng thư mục repo làm workspace root chưa (File → Open Folder)?
2. Vừa sửa `.vscode/settings.json` hoặc `~/.bashrc` → chạy `Developer: Reload Window` (`Ctrl+Shift+P`).
3. Terminal mới có đúng phải profile **ESP-IDF** không – kiểm tra dropdown mũi tên cạnh nút `+` ở panel Terminal.
4. Thấy `[idf-env] IDF_PATH chua dat` → `IDF_PATH` chưa export đúng trong `~/.bashrc`.

## Việc bắt buộc – vì sao và hậu quả nếu bỏ qua

Không có bước nào trong bảng ở README chạy "ngầm" trừ 2 hook `pre-commit`/`commit-msg` (sau khi đã cài).
Mọi dòng còn lại, nếu dev không tự gõ lệnh thì coi như chưa được kiểm tra – bộ kit này **không kèm CI**.

| Việc | Hậu quả nếu bỏ qua |
|---|---|
| `sh tools/install-hooks.sh` | 2 hook `pre-commit`/`commit-msg` không được cài → file sai format và message sai đều lọt qua ở máy đó |
| `idf.py build` sau khi sửa `.c`/`.h` | Warning lọt vào `main`; quy tắc "0 warning" chỉ còn là lời nhắc |
| `idf.py clang-check` trước khi mở PR | Lỗi bugprone/cert/clang-analyzer (tràn buffer, use-after-free...) không ai bắt trước merge |
| `/review-code` khi đổi component/bản tin/lệnh | Bỏ sót edge case, vi phạm kiến trúc phân lớp, không ai soát trước reviewer người |
| Test board thật khi đổi driver/OTA/kết nối/NVS/partition | Firmware build sạch nhưng brick/không kết nối được ngoài hiện trường |

`xargs -r` trong lệnh format file đang stage: bỏ qua, không gọi `clang-format` nếu không có file nào
đang stage (tránh lỗi `cannot use -i when reading from stdin` khi vế `git diff` rỗng). Cờ `-r` là của
GNU xargs (có sẵn trên Linux/WSL); trên macOS (BSD xargs) bỏ `-r` và tự kiểm tra danh sách rỗng trước.

## Hạn chế đã kiểm chứng của `idf.py clang-check`

**Đã kiểm chứng thật trên project thật:** `idf.py clang-check` (qua `pyclang` của ESP-IDF) tự thay
`Checks:`/`HeaderFilterRegex` bằng danh sách riêng của nó (không đọc đúng `.clang-tidy` của repo), và
danh sách cờ trình biên dịch nó tự lược bỏ **không đủ** cho cả 2 kiến trúc (thiếu `-fno-shrink-wrap`,
`-mno-relax`... mà `.clangd` đã phải lược riêng) – kết quả: **18/19 file báo lỗi "file not found"/
"unknown argument" và không phân tích được gì**.

Vì vậy: coi `warnings.txt` là công cụ **phụ, phải tự lọc nhiễu** (bỏ qua lỗi `clang-diagnostic-error` do
thiếu cờ/header, không phải lỗi code thật). Nguồn đáng tin hơn là **cảnh báo clang-tidy hiện ngay trong
VS Code qua clangd** khi gõ code (xem mục "clangd dùng nhầm bản" ở trên) – clangd tự dò đúng sysroot qua
`--query-driver`, không bị giới hạn này.

`.clang-tidy` hiện **chỉ chạy khi dev tự gõ lệnh hoặc qua clangd** – không có bước nào bắt buộc chạy lại
`idf.py clang-check` tự động trước khi merge. Nếu team dùng GitHub, có thể tự thêm workflow gọi lệnh
trên; nếu dùng Bitbucket thì viết trong `bitbucket-pipelines.yml`.

## `git ls-files` vs `git diff`

`git ls-files` liệt kê **mọi file git đang theo dõi** (đúng ý "toàn bộ project"), khác với `git diff` chỉ
liệt kê file có thay đổi. Lệnh format toàn project và lệnh kiểm tra đều loại trừ `managed_components/` –
không tự sửa code thư viện ngoài.

## File nào phục vụ tính năng Copilot nào

| File | Được nạp khi | Dùng bởi |
|---|---|---|
| `.github/copilot-instructions.md` | **Mọi** request chat/agent trong workspace | VS Code Chat/Agent, GitHub PR code review, Copilot cloud agent |
| `.github/instructions/c-esp-idf.instructions.md` | Tự động khi làm việc với `**/*.c,**/*.h` (`applyTo`) | VS Code Chat/Agent, GitHub PR code review |
| `.github/instructions/cmake-kconfig.instructions.md` | Tự động với CMakeLists/Kconfig/sdkconfig/partitions | như trên |
| `.github/instructions/testing.instructions.md` | Tự động với file trong `test/` | như trên |
| `.github/instructions/commit.instructions.md` | Nút ✨ *Generate Commit Message* (qua setting), skill `/commit`, hoặc agent tự tìm theo `description` | VS Code |
| `.github/instructions/review.instructions.md` | *Review selection / uncommitted changes* (qua setting), skill `/review-code` | VS Code |
| `.github/skills/*/SKILL.md` | Gõ `/commit`, `/review-code`, `/new-component`, `/add-message-type`, `/analyze-crash`; agent cũng tự nạp khi phù hợp `description` | VS Code Agent, cloud agent |
| `.github/agents/planner.agent.md` | Chọn agent **Planner** trong dropdown chat | VS Code |
| `.github/pull_request_template.md` | Tạo PR trên GitHub; *Generate PR description* (qua setting) | GitHub, VS Code |

`commit.instructions.md` và `review.instructions.md` **cố ý không có `applyTo`**, để không bị chèn vào mọi
request sinh code (tốn context). GitHub PR code review vẫn áp dụng quy tắc cốt lõi qua
`copilot-instructions.md` và `c-esp-idf.instructions.md`. Muốn PR review trên GitHub dùng đầy đủ checklist:
thêm `applyTo: '**'` và `excludeAgent: 'cloud-agent'` vào frontmatter của `review.instructions.md`.

## Lưu ý khác khi dùng Copilot

- **Custom instructions KHÔNG áp dụng cho inline completion** (ghost text khi gõ). Chỉ áp dụng cho Chat,
  Agent, commit/review/PR generation. Code từ inline completion vẫn phải qua clang-format, clang-tidy, review.
- Kiểm tra file nào đã được nạp: xem mục **References** ở đầu câu trả lời chat. Nếu thiếu, bật
  `github.copilot.chat.codeGeneration.useInstructionFiles`, kiểm tra cú pháp frontmatter/glob `applyTo`,
  và xem log ở *Output → GitHub Copilot Chat*.
- Các setting `github.copilot.chat.commitMessageGeneration.instructions` / `reviewSelection.instructions` /
  `pullRequestDescriptionGeneration.instructions` **bị đánh dấu deprecated từ VS Code 1.102** nhưng hiện
  vẫn là cách để nút ✨ đọc quy tắc của team. Nếu VS Code sau này gỡ bỏ chúng, dùng skill `/commit` thay thế.
  Commit message generation dùng model của `chat.utilitySmallModel` (model nhỏ) – nếu chất lượng kém, dùng
  `/commit` (chạy bằng model agent đang chọn).
- Tên thư mục skill **phải trùng** trường `name` trong `SKILL.md` (chữ thường, số, `-`), nếu không skill
  im lặng không nạp.
- Tên tool trong `planner.agent.md` (`search/codebase`, `search/usages`, `web/fetch`) theo tài liệu VS Code –
  nếu bản VS Code của bạn đổi tên, sửa qua nút **Configure Tools** trong chat.
- GitHub PR code review đọc instructions/skills từ **branch chứa thay đổi** (head branch), không phải `main`.
- `chat.tools.terminal.autoApprove` chỉ tự duyệt lệnh đọc/build an toàn; flash, erase, eFuse, push, và mọi
  lệnh có `--no-verify`/`-n` luôn phải hỏi. Không mở rộng danh sách này cho `idf.py flash`/`espefuse.py`.
