---
name: 'Commit message rules'
description: 'Quy tắc viết git commit message cho team (Conventional Commits + scope theo component). Dùng khi sinh commit message, tạo commit, hoặc viết mô tả PR.'
---

# Quy tắc commit message

Chuẩn: **Conventional Commits 1.0**, viết **tiếng Anh**, ngắn gọn nhưng **liệt kê đủ mọi thay đổi**
trong commit. Hook `tools/hooks/commit-msg` kiểm tra tự động theo đúng các quy tắc dưới đây.

## Định dạng

```
<type>(<scope>): <subject>
<dòng trống>
- <thay đổi 1>
- <thay đổi 2>
<dòng trống>
<footer>
```

### Header (bắt buộc)

- **Tối đa 72 ký tự**, không dấu chấm cuối, động từ ở dạng **mệnh lệnh** thì hiện tại, viết thường chữ đầu:
  `add`, `fix`, `handle`, `remove` (không viết `added`, `fixes`).
- `type` – chọn đúng 1:

| type | Khi nào |
|---|---|
| `feat` | Tính năng mới cho người dùng/server |
| `fix` | Sửa lỗi |
| `perf` | Cải thiện hiệu năng/năng lượng, không đổi hành vi |
| `refactor` | Tái cấu trúc, không đổi hành vi |
| `sec` | Vá hoặc tăng cường bảo mật |
| `test` | Thêm/sửa test |
| `docs` | Chỉ tài liệu |
| `build` | CMake, Kconfig, `sdkconfig.defaults`, partition, dependency (`idf_component.yml`) |
| `ci` | Pipeline CI |
| `style` | Chỉ format, không đổi logic |
| `chore` | Việc lặt vặt khác (script, .gitignore) |
| `revert` | Revert commit trước |

- `scope` – **tên component chính** bị ảnh hưởng (bắt buộc, trừ `docs`/`ci`/`chore`):
  `main`, `transport`, `mqtt`, `wifi`, `lorawan`, `rak3172`, `ota`, `config`, `cli`, `display`, `ui`,
  `button`, `sensor`, `storage`, `health`, `bsp`, `partition`, `deps`.
  Nhiều component: chọn component **chịu thay đổi chính**; nếu thực sự ngang nhau, dùng tối đa 2 scope ngăn bởi
  dấu phẩy `(mqtt,ota)`. Nhiều hơn 2 → **tách commit**.

### Body (bắt buộc nếu commit có hơn 1 thay đổi hoặc > 20 dòng diff)

- Mỗi thay đổi đáng kể là **1 gạch đầu dòng** `- `, bắt đầu bằng động từ mệnh lệnh, **tối đa 72 ký tự/dòng**.
- Nói **cái gì và vì sao**, không nói cách làm chi tiết (đã có trong diff).
- Nêu edge case/hành vi lỗi mới được xử lý.
- Nêu thay đổi ảnh hưởng tương thích: NVS schema, partition, Kconfig, topic MQTT, định dạng payload, LoRa port.

### Footer

- `BREAKING CHANGE: <mô tả>` – khi đổi định dạng payload, topic, NVS schema, partition, API public.
  Đồng thời thêm `!` sau type/scope: `feat(config)!: ...`.
- `Refs: #123` hoặc `Closes: #123` – liên kết issue.
- `Tested: <hw|bench|build|none> - <mô tả ngắn>` – **bắt buộc** với `feat`, `fix`, `perf`, `sec`.
  **Vì:** firmware build pass chưa chắc chạy đúng trên phần cứng; reviewer cần biết mức độ đã kiểm chứng.

## Nguyên tắc

- **1 commit = 1 mục đích logic.** Không trộn refactor + feat + format trong 1 commit.
- Không commit: `sdkconfig`, `build/`, `managed_components/`, secret, cert private, file `.bin`.
- Khi sinh message từ diff: đọc **toàn bộ diff đã stage**, gom thay đổi theo mục đích, không bỏ sót
  thay đổi nào ở Kconfig/CMake/partition/docs.

## Ví dụ

```
feat(lorawan): add confirmed uplink with bounded retry

- add rak3172_send_uplink_confirmed() using AT+CFM=1
- retry up to CONFIG_LORAWAN_CFM_RETRY times, backoff 2^n s + jitter
- drop message and bump tx_dropped counter when retries exhausted
- handle AT_BUSY_ERROR by waiting for +EVT:TX_DONE before resend

Refs: #42
Tested: bench - RAK3172 + ChirpStack AS923-2, 200 uplinks, 0 lost
```

```
fix(rak3172): prevent RX line overflow on long downlink

- truncate lines above RAK3172_LINE_MAXLEN and flag as invalid
- resync parser on next "\r\n" instead of dropping the whole buffer

Tested: build - no hardware available, needs bench verification
```

```
feat(config)!: move secrets to encrypted NVS partition

- add nvs_keys partition and enable NVS encryption
- migrate wifi/mqtt/lorawan secrets from app_cfg blob on first boot

BREAKING CHANGE: partition table changed, devices must be USB-flashed once
Tested: hw - ESP32-C5 devkit, migration from v1.2.0 verified
```

Ví dụ **sai**: `update code`, `fix bug`, `WIP`, `feat: many changes`, `Fixed the mqtt reconnect issue.`
