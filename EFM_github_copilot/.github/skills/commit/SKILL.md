---
name: commit
description: 'Sinh commit message theo chuẩn team từ các thay đổi đã stage (git diff --cached). Dùng khi người dùng muốn commit, viết commit message, hoặc gom thay đổi thành commit.'
argument-hint: '[issue id, ví dụ #42] [mức test: hw|bench|build|none]'
disable-model-invocation: true
---

# Sinh commit message

1. Chạy `git diff --cached --stat` và `git diff --cached` để đọc **toàn bộ** thay đổi đã stage.
   Nếu chưa stage gì: báo người dùng, liệt kê `git status --short`, **không tự `git add`**.
2. Gom thay đổi theo mục đích. Nếu có > 1 mục đích độc lập (ví dụ refactor + feat, hoặc > 2 component
   không liên quan): đề xuất tách commit và lệnh `git restore --staged <file>` tương ứng, rồi dừng.
3. Viết message đúng [quy tắc commit](../../instructions/commit.instructions.md):
   - header `<type>(<scope>): <subject>` ≤ 72 ký tự;
   - body gạch đầu dòng, mỗi dòng ≤ 72 ký tự, đủ mọi thay đổi (kể cả Kconfig/CMake/partition/docs);
   - footer `Refs:` (từ argument nếu có), `BREAKING CHANGE:` nếu đổi payload/topic/NVS/partition/API,
     `Tested:` bắt buộc với feat/fix/perf/sec – nếu người dùng không cho biết mức test, dùng
     `Tested: none - <lý do>` và **hỏi lại** thay vì tự cho là đã test.
4. Hiển thị message trong code block và **hỏi xác nhận** trước khi chạy
   `git commit -F <file tạm>`. Không `--no-verify`, không `push`.
