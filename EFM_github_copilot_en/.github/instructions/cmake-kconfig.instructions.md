---
name: 'ESP-IDF build system rules'
description: 'Rules for CMakeLists.txt, Kconfig, sdkconfig.defaults, partitions.csv, and idf_component.yml in this ESP-IDF project.'
applyTo: '**/CMakeLists.txt,**/Kconfig,**/Kconfig.projbuild,**/sdkconfig.defaults*,**/partitions*.csv,**/idf_component.yml'
---

# ESP-IDF build system rules

## Component CMakeLists.txt

```cmake
idf_component_register(
    SRCS            "rak3172.c" "rak3172_parser.c"
    INCLUDE_DIRS    "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES        esp_driver_uart          # only what the public header needs
    PRIV_REQUIRES   esp_driver_gpio esp_timer log
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
```

- `REQUIRES` only contains dependencies the **public header** needs; everything else goes in
  `PRIV_REQUIRES`. **Why:** keeps the dependency graph small, builds faster, and avoids dependency
  cycles.
- List source files explicitly; don't use `file(GLOB ...)`.
- Embed public certs/keys with `EMBED_TXTFILES`; **never** embed a private key.
- Don't use `-Wno-*` to hide a warning; fix the code instead.

## Kconfig

- Each component has its own `Kconfig`, `menu "<Component> configuration"`, named
  `<COMPONENT>_<NAME>`.
- Every hardware constant (GPIO, UART port, baud rate, SPI host, clock) and tuning parameter
  (timeout, retry, stack, priority, queue length) must be a Kconfig option with a `range` and a
  `help` text explaining its unit and the reason for the default.
- An unknown GPIO: default `-1` + a `help` text noting `TODO(confirm)`; the code must check for `-1`
  and return `ESP_ERR_INVALID_ARG` instead of crashing.
- Debug/unsafe features: `bool` defaulting to `n`, `depends on !APP_BUILD_RELEASE` if a release
  profile exists.

```kconfig
config RAK3172_UART_TX_GPIO
    int "RAK3172 UART TX GPIO (ESP32 -> module RX)"
    range -1 28
    default -1
    help
        GPIO connected to the RAK3172's RX pin. -1 = not configured yet (TODO(confirm) against the schematic).
```

## sdkconfig.defaults

- Only edit `sdkconfig.defaults` (and `sdkconfig.defaults.release` for production builds), **never
  commit `sdkconfig`**.
- Every line/group has a `#` comment explaining why it differs from the ESP-IDF default.
- Security options (Secure Boot, Flash Encryption, NVS Encryption, disabling JTAG) are only enabled
  in the release profile and require an ADR — some of these write eFuses and are
  **irreversible**.

## partitions.csv

- Always have `otadata`, `ota_0`, `ota_1` of the same size; each OTA slot ≥ the current app size +
  30% margin.
- Changing the partition layout means devices already in the field **cannot OTA** to the new layout
  → requires an ADR + `BREAKING CHANGE`.
- Align offsets to 64 KB for app partitions, 4 KB for data partitions.

## idf_component.yml

- Pin dependency versions (`"~1.2.0"` or `"==1.2.3"`), never use `"*"`.
- Commit `dependencies.lock`.
