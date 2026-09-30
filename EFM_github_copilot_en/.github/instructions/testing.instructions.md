---
name: 'Firmware testing rules'
description: 'Rules for writing unit tests (Unity), host tests (linux target), and hardware test scenarios for ESP-IDF firmware. Use when creating or editing tests.'
applyTo: '**/test/**,**/test_apps/**,**/host_test/**'
---

# Testing rules

## Three-tier strategy

| Tier | Tooling | Covers | Runs on |
|---|---|---|---|
| Host test | ESP-IDF `linux` target + Unity, or CMock | Pure logic: AT parser, payload encode/decode, state machine, ring buffer, config validation | CI, every PR |
| Target unit test | `test_apps/` + `pytest-embedded` | Drivers against real hardware (UART loopback, SPI, NVS) | Bench, before release |
| System/HIL test | Manual scenarios or scripts | LoRaWAN join, MQTT reconnect, OTA + rollback, 72h soak test | Before release |

**Why:** most bugs live in pure logic (parsers, encoders) — isolate them from hardware dependencies
so they can be tested quickly on the host. For this to be possible, business logic must receive its
dependencies through an interface (Strategy/function pointers), never call a driver directly.

## Rules for writing tests

- Test name: `TEST_CASE("<unit>: <behavior> when <condition>", "[<component>]")`.
- Each test checks **one behavior**, following an Arrange – Act – Assert structure.
- Tests are required for: valid input, **boundary values** (0, max, max+1), malformed input, NULL,
  timeout, calling before init.
- AT parser: test with a truncated line, garbage characters, `\r` without `\n`, a line longer than
  the buffer, and multiple `+EVT` lines interleaved with a synchronous response.
- Tests must not depend on run order; clean up resources (`tearDown`), and check for heap leaks
  (`unity_utils_check_leak` / comparing `heap_caps_get_free_size` before and after).
- Don't use a long `vTaskDelay` "just to be safe" — use explicit synchronization (a semaphore) with
  a timeout.

## Minimum hardware test scenarios before release

1. First boot with empty NVS → falls back to default config, CLI works.
2. WiFi drops for 10 minutes → reconnects automatically, no data lost within the queue's limit.
3. MQTT server goes down/up → reconnects with backoff.
4. RAK3172 resets mid-send → driver resyncs, rejoins.
5. Successful OTA; OTA with a corrupt/wrongly-signed image → rejected; an image that fails to boot →
   rolls back.
6. Power loss during an OTA and during an NVS write → boots normally afterward.
7. Soak test ≥ 72 hours: minimum heap stays stable, no unexpected reset (`esp_reset_reason`).
8. Display disconnected → firmware keeps running, logs one warning.
