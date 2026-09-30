---
name: 'ESP-IDF build system rules'
description: 'Quy tắc cho CMakeLists.txt, Kconfig, sdkconfig.defaults, partitions.csv, idf_component.yml của dự án ESP-IDF.'
applyTo: '**/CMakeLists.txt,**/Kconfig,**/Kconfig.projbuild,**/sdkconfig.defaults*,**/partitions*.csv,**/idf_component.yml'
---

# Quy tắc build system ESP-IDF

## CMakeLists.txt của component

```cmake
idf_component_register(
    SRCS            "rak3172.c" "rak3172_parser.c"
    INCLUDE_DIRS    "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES        esp_driver_uart          # chỉ những gì header public cần
    PRIV_REQUIRES   esp_driver_gpio esp_timer log
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
```

- `REQUIRES` chỉ chứa dependency mà **header public** cần; còn lại để `PRIV_REQUIRES`.
  **Vì:** giữ dependency graph nhỏ, build nhanh, tránh vòng phụ thuộc.
- Liệt kê file nguồn tường minh, không dùng `file(GLOB ...)`.
- Cert/khóa public nhúng bằng `EMBED_TXTFILES`; **không** nhúng private key.
- Không dùng `-Wno-*` để giấu warning; sửa code.

## Kconfig

- Mỗi component có `Kconfig` riêng, `menu "<Component> configuration"`, tên `<COMPONENT>_<NAME>`.
- Mọi hằng số phần cứng (GPIO, UART port, baud, SPI host, clock) và tham số tuning (timeout, retry, stack,
  priority, queue length) phải là Kconfig có `range` và `help` giải thích đơn vị + lý do default.
- GPIO chưa biết: default `-1` + `help` ghi `TODO(confirm)`; code phải kiểm tra `-1` và trả
  `ESP_ERR_INVALID_ARG` thay vì crash.
- Tính năng debug/không an toàn: `bool` default `n`, `depends on !APP_BUILD_RELEASE` nếu có profile release.

```kconfig
config RAK3172_UART_TX_GPIO
    int "RAK3172 UART TX GPIO (ESP32 -> module RX)"
    range -1 28
    default -1
    help
        GPIO noi toi chan RX cua RAK3172. -1 = chua cau hinh (TODO(confirm) theo schematic).
```

## sdkconfig.defaults

- Chỉ sửa `sdkconfig.defaults` (và `sdkconfig.defaults.release` cho build production), **không commit
  `sdkconfig`**.
- Mỗi dòng/nhóm có comment `#` giải thích vì sao đổi so với mặc định ESP-IDF.
- Option bảo mật (Secure Boot, Flash Encryption, NVS Encryption, JTAG disable) chỉ bật trong profile
  release và phải có ADR – một số thay đổi ghi eFuse **không thể đảo ngược**.

## partitions.csv

- Luôn có `otadata`, `ota_0`, `ota_1` cùng kích thước; slot OTA ≥ kích thước app hiện tại + 30% dư.
- Đổi partition = thiết bị ngoài hiện trường **không OTA được** layout mới → cần ADR + `BREAKING CHANGE`.
- Offset căn chỉnh 64 KB cho phân vùng app, 4 KB cho data.

## idf_component.yml

- Khoá version dependency (`"~1.2.0"` hoặc `"==1.2.3"`), không dùng `"*"`.
- Commit `dependencies.lock`.
