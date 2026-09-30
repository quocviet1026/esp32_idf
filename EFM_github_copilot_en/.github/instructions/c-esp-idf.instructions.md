---
name: 'C / ESP-IDF coding rules'
description: 'Rules for writing C code for the ESP32-C5 + ESP-IDF firmware: naming, error handling, memory, FreeRTOS, edge cases, security. Use when creating or editing .c/.h files.'
applyTo: '**/*.c,**/*.h'
---

# C / ESP-IDF coding rules

Goal: code that is **correct, readable, reusable, and runs reliably for years** on an unattended
device. Every rule comes with a reason (`Why:`) so you can apply it correctly in situations not
explicitly covered here.

## 1. File layout

Required order in a `.c` file:

```c
/* <filename>.c
 *
 * Module responsibility (1-3 sentences), threading constraints, resource ownership.
 */

#include <string.h> /* 1. C standard */
#include <stdint.h>

#include "freertos/FreeRTOS.h" /* 2. FreeRTOS / ESP-IDF */
#include "esp_log.h"
#include "esp_check.h"

#include "transport.h" /* 3. another component in the project */

#include "sensor_internal.h" /* 4. this component's own internal header */

static const char *TAG = "sensor"; /* TAG = component name */

/* macro/const -> internal types -> static variables -> static prototypes -> static functions -> public functions */
```

Header `.h`:

```c
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public API, every function commented: what it does, parameters, return value, thread-safety, calling context */

#ifdef __cplusplus
}
#endif
```

- Public headers only live in `components/<x>/include/`, and only contain what other components
  need. Internal types/functions go in `<x>_internal.h` (not under `include/`). **Why:** reduces
  coupling, so changing the implementation doesn't affect other places.
- A header must be self-contained: it must compile on its own with no errors.

## 2. Naming

| Kind | Convention | Example |
|---|---|---|
| Public function | `<component>_<verb>[_<object>]` | `rak3172_send_uplink()` |
| Static function | `snake_case`, no prefix needed | `parse_evt_line()` |
| File-scope static variable | `s_` + snake_case | `s_cmd_lock` |
| Global variable (keep to a minimum) | `g_` + snake_case, only in `*_internal.h` | `g_transport_mqtt` |
| Type | `snake_case_t` | `app_message_t` |
| Enum value / macro | `UPPER_CASE` with a component prefix | `APP_MSG_SENSOR`, `RAK3172_LINE_MAXLEN` |
| Callback type | `<component>_<event>_cb_t` | `rak3172_join_cb_t` |
| Opaque handle | `<component>_handle_t` | `button_handle_t` |
| Kconfig | `CONFIG_<COMPONENT>_<NAME>` | `CONFIG_RAK3172_TX_GPIO` |

Put the unit in the variable name: `timeout_ms`, `interval_s`, `len_bytes`, `temp_centi_c`.
**Why:** mixing up units (ms vs. ticks, bytes vs. elements) is the most common bug in firmware.

## 3. Error handling

- A function that can fail returns `esp_err_t`; data is returned through an output pointer (`out_`
  prefix).
- Validate parameters at the top of public functions, using the `esp_check.h` macros:

```c
esp_err_t sensor_read(sensor_handle_t h, sensor_sample_t *out_sample)
{
    ESP_RETURN_ON_FALSE(h != NULL && out_sample != NULL, ESP_ERR_INVALID_ARG, TAG, "null arg");
    ESP_RETURN_ON_FALSE(h->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    esp_err_t ret = ESP_OK;
    uint8_t raw[SENSOR_FRAME_LEN] = { 0 };

    ESP_GOTO_ON_ERROR(bus_lock(h, pdMS_TO_TICKS(SENSOR_BUS_TIMEOUT_MS)), exit, TAG, "bus busy");
    ESP_GOTO_ON_ERROR(bus_read(h, raw, sizeof(raw)), exit_unlock, TAG, "bus read");
    ESP_GOTO_ON_FALSE(frame_crc_ok(raw, sizeof(raw)), ESP_ERR_INVALID_CRC, exit_unlock, TAG, "bad crc");
    decode_frame(raw, out_sample);

exit_unlock:
    bus_unlock(h);
exit:
    return ret;
}
```

- A function that owns a resource needing cleanup: use a **single exit point** with a `goto` cleanup
  (the standard ESP-IDF pattern). **Why:** avoids leaking mutexes/memory when a new error branch is
  added later.
- `ESP_ERROR_CHECK()` is only for **unrecoverable errors during boot** (e.g. `nvs_flash_init` after
  already trying an erase). Never use it in a runtime loop. **Why:** abort = reboot = data loss and
  possibly a boot loop.
- A runtime error must have a **recovery path**: bounded retry + backoff, falling back to a safe
  state, or reporting up to the caller. Never swallow an error silently; at minimum, log it with
  `ESP_LOGW` and `esp_err_to_name(err)`.
- Defining your own error codes: use an `ESP_ERR_<COMPONENT>_BASE` base, or reuse a standard code
  (`ESP_ERR_TIMEOUT`, `ESP_ERR_INVALID_RESPONSE`, `ESP_ERR_INVALID_SIZE`...).

## 4. Memory & buffers

- **Prefer static allocation**; if heap is needed, allocate **once at init**, never `malloc/free`
  inside a loop that runs forever. **Why:** heap fragmentation after a few weeks makes `malloc` fail
  randomly.
- If dynamic allocation is unavoidable: check for `NULL`, use `heap_caps_malloc(size, MALLOC_CAP_...)`
  when DMA/internal RAM is required, free it in exactly one place, and set the pointer to `NULL`
  after freeing.
- Forbidden: `strcpy`, `strcat`, `sprintf`, `gets`, `atoi`, `strtok`. Use instead: `strlcpy`,
  `snprintf` (check the return value ≥ size → truncation), `strtol/strtoul` (check `endptr`, `errno`,
  range), `strtok_r`.
- Always use `sizeof(buf)`, never repeat a size constant. For static arrays: `ARRAY_SIZE(a)` =
  `sizeof(a)/sizeof((a)[0])`.
- Check for arithmetic overflow before adding/multiplying lengths (`if (len > sizeof(buf) - offset)`),
  never write `offset + len > size` where the addition itself can overflow.
- Strings received from outside may **not be NUL-terminated**: use an explicit length (`%.*s`,
  `memchr`).
- DMA buffers for the SPI LCD: allocate with `MALLOC_CAP_DMA`, aligned per the driver's requirements.
- Structs stored in NVS or sent over the network: don't rely on the compiler's layout/padding →
  serialize explicitly (byte order, fixed sizes), or use a `magic + version + crc32` header like
  `app_config_t`.

## 5. FreeRTOS & concurrency

- Every task's parameters are declared explicitly via a macro/Kconfig: `*_TASK_STACK`,
  `*_TASK_PRIO`, core (the C5 is single-core → use `xTaskCreate`). Comment the reasoning behind the
  chosen stack size; after measuring with `uxTaskGetStackHighWaterMark()`, keep a margin of ≥ 25%.
- Tasks communicate via a **queue/event group/esp_event**, never through a shared global variable.
  A variable shared between tasks must be protected by a mutex or be `_Atomic`/an atomic type, with
  a comment stating who writes and who reads it.
- **Never block inside**: an ISR, an esp-mqtt/esp_event/esp_timer callback, or a driver's RX task.
  Only copy data → `xQueueSend(..., 0)` → handle it in a different task. ISRs only use the
  `...FromISR` APIs and `portYIELD_FROM_ISR`. Mark ISR functions `IRAM_ATTR` when needed.
- Every `xSemaphoreTake/xQueueReceive/xEventGroupWaitBits` has a **finite timeout**, and the timeout
  branch is handled.
- Lock acquisition order is fixed and documented at the top of the file; never call a user-supplied
  callback while holding a lock. **Why:** avoids deadlock (e.g. calling `rak3172_send_cmd()` from
  inside a rak3172 callback).
- Ticks and time: use `esp_timer_get_time()` (int64, µs), or compare the difference
  `(now - start) >= period` with an unsigned type. Never compare `now > deadline` directly with a
  `uint32_t`. **Why:** a 32-bit tick counter overflows after ~49 days at 1 kHz.
- A task that runs forever must have a `vTaskDelay`/a bounded wait inside its loop, and must
  register with the **Task WDT** (`esp_task_wdt_add`) if it's a critical task.

## 6. Long-running robustness

- Network connections (WiFi, MQTT, LoRaWAN join): **exponential backoff with jitter**, capped at an
  upper bound, reset the backoff on success. Never retry in a tight burst.
- The outbound message queue has a **bound** and a policy for when it's full (drop oldest + count
  dropped messages).
- Writing to NVS: only write when the value **actually changes**, never on a short fixed cycle.
  **Why:** flash wear.
- Logging: don't log at INFO level inside a fast loop; rate-limit repeated error logs. Never log
  large payloads.
- Health monitoring: minimum heap (`esp_get_minimum_free_heap_size`), stack watermark, reconnect
  count, reset reason (`esp_reset_reason`) → feed into a diagnostics message.
- New firmware after an OTA must self-confirm (`esp_ota_mark_app_valid_cancel_rollback`) **only
  after** successfully connecting to the server; have a watchdog that auto-rolls-back if
  confirmation never happens.

## 7. Mandatory edge cases to consider

For every function/feature, handle these explicitly (in code, or with a comment explaining why it's
skipped):

- [ ] `NULL` parameters, zero length, maximum length, length beyond the maximum.
- [ ] Called before `init`, `init` called twice, called after `deinit`.
- [ ] Timeout, unresponsive peer, incomplete/extra/malformed response (AT: `AT_ERROR`,
      `AT_BUSY_ERROR`, `AT_NO_NETWORK_JOINED`, a truncated line, garbage characters after a module
      reset).
- [ ] WiFi/MQTT/LoRa dropping mid-operation; the server sending a duplicate or stale (replayed)
      command.
- [ ] Power loss/reset while writing NVS or writing an OTA image.
- [ ] Sensor value outside its physical range, sensor not responding, NaN/Inf with `float`.
- [ ] Buttons: bounce, long press, rapid repeated presses, pressed during boot.
- [ ] Display: SPI error, display not attached → the system must keep running regardless (the
      display is a secondary feature).
- [ ] Counter overflow (tick, sequence number, frame counter).

## 8. Security

- MQTT only over `mqtts://` with an embedded CA cert (`EMBED_TXTFILES`) + hostname verification. OTA
  over HTTPS + image signature check (Secure Boot V2 / signed app) + `secure_version` to prevent
  downgrades.
- Secrets (WiFi password, MQTT password, AppKey) stored in **encrypted NVS**; never printed to
  CLI/logs (shown as `****`).
- Downlink/OTA commands: validate format, length, range, and system state before executing; reject
  invalid commands and log a warning (without logging sensitive content).
- Don't expose dangerous debug/CLI APIs in release builds (`#if CONFIG_APP_DEBUG_CLI`).
- Clear buffers holding secrets after use (`memset` through a `volatile` pointer, or
  `mbedtls_platform_zeroize`).

## 9. Preferred design patterns

| Pattern | Use when | Example in this project |
|---|---|---|
| Strategy (struct of function pointers) | Multiple implementations of one interface | `transport_if_t`, `sensor_driver_t` |
| Table-driven dispatch | Mapping a type to a handler | `s_publish_routes[]`, the AT command table, the downlink command table |
| Explicit state machine (`enum` + `switch`/transition table) | Connection, join, OTA, UI | `conn_state_t`, `ota_state_t` |
| Observer (`esp_event` / registered callback) | Reporting an event up a layer | `APP_EVENT_SENSOR_READY`, button events |
| Producer–Consumer (queue) | Decoupling an ISR/callback from processing | RAK3172 RX task → queue → service |
| Opaque handle | A driver with multiple instances | `button_handle_t`, `lcd_handle_t` |
| Facade | Hiding a multi-step procedure | `ota_manager_start()` |

Don't introduce an abstraction when there's only one implementation and no plan to add more — write
a `TODO` instead of over-engineering.

## 10. Comments & documentation

- Comments explain **why**, constraints, and threading context; they don't repeat the code.
- Every public function: description, parameters (with units), possible return values, whether it's
  thread-safe, and whether it may be called from an ISR/callback.
- A value not yet verified on hardware: `/* TODO(confirm): ... */` — reviewers search for this tag.
