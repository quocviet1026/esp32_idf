---
name: review-code
description: 'Review code firmware ESP-IDF theo checklist của team (đúng đắn, concurrency, bộ nhớ, độ bền dài hạn, bảo mật, edge case). Dùng khi người dùng yêu cầu review thay đổi hiện tại, 1 file, 1 branch hoặc 1 PR.'
argument-hint: '[file | branch | "staged"] - mặc định: thay đổi chưa commit'
---

# Review code

1. Xác định phạm vi:
   - không có argument → `git diff HEAD` (cả staged và unstaged);
   - `staged` → `git diff --cached`;
   - tên branch → `git diff origin/main...<branch>`;
   - đường dẫn file → toàn bộ file đó.
2. Với mỗi hàm bị thay đổi, **đọc cả nơi gọi và nơi được gọi** (tìm usage) để hiểu context threading,
   ownership buffer, và vòng đời – không review cô lập từng dòng diff.
3. Áp dụng [checklist review](../../instructions/review.instructions.md) và
   [quy tắc C/ESP-IDF](../../instructions/c-esp-idf.instructions.md).
4. Chỉ báo vấn đề có **kịch bản lỗi cụ thể**. Tự kiểm lại từng phát hiện trước khi báo: nếu code
   đã xử lý ở nơi khác thì bỏ.
5. Trình bày đúng format trong checklist (mức độ, file:line, kịch bản, đề xuất diff), kết thúc bằng
   Approve / Request changes. **Không tự sửa code** trừ khi người dùng yêu cầu.
