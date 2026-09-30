---
name: 'Code review checklist'
description: 'Checklist for reviewing ESP-IDF firmware code (correctness, concurrency, memory, long-term robustness, security, edge cases). Use when asked to review a diff, a PR, a file, or a selected block of code.'
---

# Code review guide

You act as a **demanding reviewer** for firmware that runs for years in the field. The goal is to
find **real, reproducible** bugs, not to list subjective style opinions (style is already handled by
`clang-format`).

## How to present results

For each issue:

```
[SEVERITY] file.c:line – one-line summary
Failure scenario: specific input/state → consequence (crash, hang, leak, corrupted data, vulnerability)
Suggestion: a short diff or a description of the fix
```

Severity levels:

- **BLOCKER** – crash, deadlock, memory corruption, security vulnerability, bricked device, loss of
  OTA/rollback capability.
- **MAJOR** – resource leak, no recovery from a network error, an unhandled edge case that can
  realistically occur, a violation of the layering architecture.
- **MINOR** – hard to maintain, missing an important log/comment, naming that violates the
  conventions.
- **NIT** – a small, optional suggestion.

List BLOCKERs first. If no significant issue is found in a category, **don't invent one** — write
"No issues found". End with: totals per severity level + **Conclusion: Approve / Request changes**.

## Checklist

### Correctness
- [ ] Every `esp_err_t` return value is checked; error branches release the right resources (mutex,
      heap, handles).
- [ ] No use of an uninitialized variable; a `switch` on an enum has every case or a sensible
      `default`.
- [ ] Units are correct (ms ↔ ticks via `pdMS_TO_TICKS`, bytes ↔ elements).
- [ ] Time comparisons are safe against counter overflow.
- [ ] `float`: NaN/Inf are handled, no comparison with `==`.

### Memory
- [ ] No unbounded string function (`strcpy`, `sprintf`, `strcat`); `snprintf` checks for
      truncation.
- [ ] Length is checked before `memcpy`; no arithmetic overflow when computing an offset.
- [ ] No `malloc` inside a runtime loop; every `malloc` is checked for NULL and has exactly one
      matching `free`.
- [ ] No returning/storing a pointer to a temporary buffer (on the stack, or owned by a callback).
- [ ] Task stack size is reasonable relative to large local buffers (a buffer > 256 bytes on the
      stack needs a second look).

### Concurrency / FreeRTOS
- [ ] Shared data is protected (mutex/atomic/critical section), with a comment on who writes and who
      reads it.
- [ ] No blocking inside an ISR, an esp-mqtt/esp_event/esp_timer callback, or a driver's RX task.
- [ ] Every wait has a finite timeout, and the timeout branch is handled.
- [ ] No API call that could re-acquire the same lock from inside a callback (deadlock).
- [ ] ISRs only use the `FromISR` APIs, with `IRAM_ATTR` if they need to run while the flash cache is
      disabled.

### Long-term robustness
- [ ] Reconnects use backoff + jitter + a cap; never retry indefinitely in a tight burst.
- [ ] Queues/buffers are bounded, with a policy for when full.
- [ ] No writing to NVS/flash on a short fixed cycle.
- [ ] Critical tasks register with the Task WDT; the WDT is never disabled.
- [ ] Logging doesn't flood; nothing is logged from an ISR.
- [ ] OTA: doesn't break the mark-valid/rollback flow; OTA is blocked while a critical operation is
      in progress or the battery is low.

### Security
- [ ] No hard-coded/logged secrets; the CLI masks secrets.
- [ ] TLS checks the cert and hostname; no `skip_cert_common_name_check` or `INSECURE` flag.
- [ ] Input from outside (MQTT/LoRa downlink, AT responses, CLI, NVS) is validated for
      length/range/format.
- [ ] Commands from the server are validated, and protected against replay/duplication if they have
      a physical effect or change configuration.
- [ ] No dangerous debug/JTAG/CLI feature is enabled in the release configuration.

### Architecture & reuse
- [ ] No violation of the layering (a driver never includes a service/app header).
- [ ] A new message/command type is added via the routing table, not a new public function per type.
- [ ] No duplicated logic that already exists in another component (search before writing new code).
- [ ] Public APIs are adequately commented: parameters, units, possible errors, thread-safety.

### Build & documentation
- [ ] A change to Kconfig/partitions/NVS schema/topic/payload updates `docs/`, and (if it breaks
      compatibility) has an ADR.
- [ ] Hardware values not yet verified are marked `TODO(confirm):`.
- [ ] The commit message follows `.github/instructions/commit.instructions.md`.
