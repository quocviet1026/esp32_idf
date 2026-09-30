## Mục đích

<!-- 1-3 câu: thay đổi gì, vì sao. Liên kết issue: Closes #123 -->

## Thay đổi chính (theo component)

- `component`: ...

## Ảnh hưởng tương thích

- [ ] Không có
- [ ] NVS schema (`APP_CONFIG_VERSION` đã bump, có migration/về default an toàn)
- [ ] Partition table (thiết bị hiện trường **không** OTA được – cần flash USB)
- [ ] Định dạng payload / topic MQTT / LoRa fPort (server cần cập nhật)
- [ ] Kconfig / `sdkconfig.defaults`
- [ ] Bảo mật: Secure Boot / Flash Encryption / eFuse (**không đảo ngược được** – có ADR: `docs/adr/...`)

## Edge case đã xử lý

<!-- timeout, mất kết nối, dữ liệu sai định dạng, gọi trước init, mất điện giữa chừng... -->

## Kiểm thử

| Mức | Kết quả |
|---|---|
| Build `esp32c5` 0 warning | ☐ |
| Host test | ☐ |
| Bench (board thật) – mô tả | ☐ |
| OTA + rollback (nếu đụng OTA/boot) | ☐ |
| Soak test (nếu đụng task/bộ nhớ/kết nối) – số giờ | ☐ |

**Chưa kiểm chứng trên phần cứng:** <!-- ghi rõ, hoặc "Không" -->

## Checklist

- [ ] Commit message đúng quy tắc (`tools/hooks/commit-msg` pass)
- [ ] Đã tự review theo `.github/instructions/review.instructions.md`
- [ ] Cập nhật `docs/` nếu đổi kiến trúc/dataflow
- [ ] Không có secret, `sdkconfig`, file build trong diff
