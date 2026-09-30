# Yêu cầu tính năng: Cấu hình Static IP qua Serial Console (ESP32)

**Dự án:** ESP32 (WiFi) + LoRa
**Ngày:** 18/09/2026
**Người yêu cầu:** Viet Nguyen Quoc

---

## 1. Mục tiêu (User Story)

Mở rộng serial console hiện tại để cho phép cấu hình **static IP networking**, với các thông số được **lưu trữ (persist) vào NVS**.

Thiết bị phải hỗ trợ **2 chế độ**:
- **Auto** – cấu hình IP tự động qua DHCP (hành vi hiện tại).
- **Manual** – cấu hình IP thủ công: địa chỉ IP, subnet mask, default gateway, và DNS server (tùy chọn).

---

## 2. Đặc tả kỹ thuật

### 2.1. Lệnh console mới

```
IPCONFIG <auto|manual> <IP> <subnet_mask> <default_gateway> [<DNS1>] [<DNS2>]
```

### 2.2. Logic xử lý theo chế độ

| Mode | Hành vi |
|---|---|
| `auto` | Giữ nguyên hành vi DHCP hiện tại (không thay đổi) |
| `manual` | 1. Gọi `esp_netif_dhcpc_stop()` <br> 2. Gọi `esp_netif_set_ip_info()` để set IP/mask/gateway <br> 3. Gọi `esp_netif_set_dns_info()` nếu có DNS1/DNS2 |

### 2.3. Hiện trạng code

- File `WiFi.c` hiện tại chỉ khởi tạo **một STA netif mặc định dùng DHCP**.
- **Chưa có** nhánh xử lý static IP → cần bổ sung mới hoàn toàn.

### 2.4. Yêu cầu lưu trữ (Persistence)

- Mode (`auto`/`manual`) và toàn bộ config (IP, mask, gateway, DNS) phải được **lưu vào NVS**.
- Khi thiết bị khởi động lại, config phải được **đọc từ NVS và áp dụng lại trong quá trình WiFi init**.

---

## 3. Tiêu chí chấp nhận (Acceptance Criteria)

1. `IPCONFIG auto` khôi phục hành vi DHCP giống hệt hiện tại.
2. `IPCONFIG manual` yêu cầu bắt buộc: IP, subnet mask, gateway. DNS1/DNS2 là tùy chọn.
3. Nếu tham số IP không hợp lệ/sai định dạng → báo lỗi rõ ràng, **không được crash**.
4. Cấu hình phải **tồn tại sau khi reboot** (lưu trong NVS) và được áp dụng lại lúc WiFi init.
5. Thiết bị phải **kết nối thành công** vào mạng không có DHCP server khi dùng cấu hình manual.

---

## 4. Lưu ý khi triển khai trong dự án ESP32 + LoRa

- **Ứng dụng thực tế:** static IP thường dùng cho gateway kết nối tới server/MQTT broker cố định trong LAN nội bộ, hoặc mạng công nghiệp không có DHCP server.
- **NVS namespace:** nên tạo namespace riêng (ví dụ `net_cfg`) chứa struct `{mode, ip, netmask, gw, dns1, dns2}`, tránh xung đột với NVS key của LoRa config/keys.
- **Thứ tự khởi tạo:** phải đọc NVS **trước khi** netif start DHCP; cần xử lý tránh race condition giữa `dhcpc_stop()` và event `WIFI_EVENT_STA_START`.
- **Validate IP input:** dùng `esp_netif_str_to_ip4()` để parse và kiểm tra hợp lệ trước khi gọi các hàm set, đảm bảo đáp ứng AC #3.
- **Không ảnh hưởng LoRa stack:** đảm bảo console/task xử lý lệnh IPCONFIG không conflict với LoRa task (đặc biệt nếu dùng chung UART hoặc console handler).

---

## 5. Việc cần làm (Task checklist)

- [ ] Định nghĩa struct config trong NVS (`net_cfg` namespace)
- [ ] Viết hàm load config từ NVS khi boot
- [ ] Viết hàm save config vào NVS khi nhận lệnh IPCONFIG
- [ ] Đăng ký lệnh console `IPCONFIG` (parser + validate tham số)
- [ ] Implement nhánh xử lý `auto` (giữ nguyên DHCP)
- [ ] Implement nhánh xử lý `manual` (dhcpc_stop → set_ip_info → set_dns_info)
- [ ] Validate IP/mask/gateway/DNS input, xử lý lỗi không crash
- [ ] Áp dụng config đã lưu trong quá trình WiFi init lúc boot
- [ ] Test: auto mode, manual mode, input sai, reboot persistence, mạng không DHCP
