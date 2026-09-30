# EFM – Bộ cấu hình GitHub Copilot cho team firmware

Bộ file cấu hình Copilot, công cụ format/lint, quy trình commit và tài liệu kiến trúc cho dự án
**ESP32-C5 + RAK3172** (ESP-IDF v5.5.x, C, VS Code). Copy toàn bộ vào root repo firmware.

> Chi tiết/lý do cho mọi mục dưới đây nằm ở [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Cài đặt (mỗi dev, 1 lần)

```bash
cp -r .github .vscode docs tools .clang-format .clang-tidy .clangd .editorconfig .gitmessage .gitignore <repo>/
cd <repo>
sh tools/install-hooks.sh                   # commit-msg hook + commit template
export IDF_PATH="$HOME/esp-idf"             # thêm dòng này vào ~/.bashrc
idf.py set-target esp32c5 && idf.py build   # tạo build/compile_commands.json cho clangd
```

Cài extension được đề xuất khi VS Code hỏi (`.vscode/extensions.json`). Tắt IntelliSense của
`ms-vscode.cpptools` (đã cấu hình `C_Cpp.intelliSenseEngine: disabled`) để chỉ dùng clangd, tránh 2 bộ
phân tích chạy song song.

**Kiểm tra clangd dùng đúng bản** (bắt buộc, nhất là board Xtensa): mở terminal ESP-IDF (mục dưới), gõ
`which clangd` – phải trỏ vào `.../esp-clang/.../bin/clangd`. Nếu không, xem mục "clangd dùng nhầm bản"
trong [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Mở terminal ESP-IDF

Terminal mặc định (profile **ESP-IDF**) đã tự nạp môi trường – mở terminal mới, thấy
`[idf-env] ESP-IDF v5.5.x ready` là dùng `idf.py` được ngay, không cần tự `. $IDF_PATH/export.sh`.
Chưa biết `IDF_PATH`, hoặc không thấy dòng đó? Xem [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Việc bắt buộc phải làm

Bộ kit **không kèm CI** – không có lớp nào khác chặn hộ, bỏ qua bước nào thì lỗi lọt thẳng vào `main`.

| Khi nào | Lệnh | Tự động? |
|---|---|---|
| 1 lần, ngay sau khi có repo | `sh tools/install-hooks.sh` | Không |
| Mỗi khi sửa `.c`/`.h`, trước khi coi là xong | `idf.py build` – phải **0 warning** | Không |
| Trước mỗi commit | *(không cần gõ gì – 2 hook tự chạy: kiểm tra format rồi kiểm tra message)* | **Có**, sau khi đã làm bước 1 |
| Trước khi mở PR / coi task hoàn thành | `idf.py clang-check --exclude-paths managed_components`, đọc `warnings.txt` (có hạn chế đã biết) | Không |
| Trước khi mở PR, nếu đổi component/bản tin/lệnh | `/review-code` (hoặc `/review-code staged`) trong Copilot Chat | Không |
| Trước khi merge, nếu đổi driver/OTA/kết nối/NVS/partition | Test trên board thật theo checklist trong `pull_request_template.md` | Không |

Nếu commit bị chặn vì sai format: `git diff --name-only --cached -- '*.c' '*.h' | xargs -r clang-format -i`
rồi `git add` lại. Bật `editor.formatOnSave` (đã bật sẵn) thì hầu như không bao giờ gặp trường hợp này.

## Quy trình làm việc khuyến nghị

1. **Tính năng lớn** → agent **Planner** → thống nhất kế hoạch → *Triển khai kế hoạch* (handoff sang Agent).
2. **Thêm component/bản tin** → `/new-component ...`, `/add-message-type ...`.
3. **Trước khi commit** → `/review-code` (hoặc `/review-code staged`).
4. **Commit** → nút ✨ trong Source Control hoặc `/commit #42 bench`.
5. **Crash** → dán log vào chat với `/analyze-crash`.

## Chạy clang-format

`clang-format` có sẵn trong toolchain `esp-clang`, dùng được ngay từ terminal ESP-IDF, không cần cài thêm.

| Muốn làm gì | Lệnh |
|---|---|
| Toàn bộ project | `clang-format -i $(git ls-files '*.c' '*.h' ':!:managed_components/**')` |
| Chỉ file đang stage (trước commit) | `git diff --name-only --cached -- '*.c' '*.h' \| xargs -r clang-format -i` |
| Chỉ kiểm tra, không sửa | `clang-format --dry-run --Werror $(git ls-files '*.c' '*.h' ':!:managed_components/**')` |
| 1 file cụ thể | `clang-format -i main/main.c` |
| Tự động, không gõ lệnh | Mở file `.c/.h`, `Ctrl+S` (`editor.formatOnSave` đã bật sẵn) |

**Format code đã có sẵn (vd repo giai đoạn 1):** đừng chạy tuỳ tiện, diff format sẽ trộn lẫn thay đổi
logic khác rất khó review. Làm theo đúng thứ tự trong
[copilot-onboarding-legacy.md, giai đoạn E2](docs/prompt/copilot-onboarding-legacy.md): viết test bảo vệ
hành vi hiện tại → format → `idf.py build` kiểm tra không đổi hành vi → commit **riêng biệt**
(`style: apply clang-format to phase1 code`) → thêm vào `.git-blame-ignore-revs`.

## Tài liệu

- [docs/architecture.md](docs/architecture.md), [docs/dataflow.md](docs/dataflow.md), [docs/security.md](docs/security.md) –
  **khung đề mục**; agent điền nội dung từ code phase 1 theo giai đoạn C của
  [copilot-onboarding-legacy.md](docs/prompt/copilot-onboarding-legacy.md)
- [docs/adr/](docs/adr/) – ghi lại quyết định kiến trúc
- [docs/prompt/copilot-onboarding-legacy.md](docs/prompt/copilot-onboarding-legacy.md) – các bước + prompt để Copilot hiểu repo giai đoạn 1 trước khi refactor/thêm tính năng
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) – lý do, hạn chế đã kiểm chứng, cách các file Copilot hoạt động
