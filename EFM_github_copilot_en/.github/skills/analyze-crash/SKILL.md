---
name: analyze-crash
description: 'Analyze an ESP32-C5 crash/panic/watchdog/reset log (Guru Meditation, RISC-V backtrace, stack overflow, TWDT, brownout) and propose the root cause + a fix. Use when the user pastes an error log or the device resets unexpectedly.'
argument-hint: '<paste the monitor log, or a path to a log file>'
---

# Analyze a crash

1. Identify the type of failure from the log: `Guru Meditation Error` (exception type, `MEPC`,
   `MTVAL`), `Stack protection fault`/`stack overflow in task`, `Task watchdog got triggered`,
   `Brownout detector`, `abort() was called` (look for an assert/`ESP_ERROR_CHECK` above it), the
   `rst:` reason in the boot log.
2. Decode the addresses: if the log hasn't been decoded by `idf.py monitor`, use
   `riscv32-esp-elf-addr2line -pfiaC -e build/<app>.elf <addr...>` with **the exact ELF file of the
   build that was running** (check the `App version`/`ELF file SHA256` in the boot log matches the
   build).
3. Read the code at each frame, tracing back to the data that caused the failure. Common hypotheses
   by category:
   - Load/Store access fault, `MTVAL` near 0 → a NULL pointer / an uninitialized struct.
   - Stack overflow → a large buffer on the stack, recursion, `printf` of a float in a task with a
     small stack.
   - TWDT → a loop that never yields, a wait with no timeout, a lock held too long, a large flash
     erase.
   - A crash appearing at random after many hours → heap corruption, use-after-free, a race
     condition, a buffer overflow.
4. Answer with: the root cause (with a confidence level), the evidence from the log, a suggested fix
   as a diff, and how to verify it (enable `CONFIG_HEAP_POISONING_COMPREHENSIVE`,
   `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK`, a core dump to flash). If the log isn't enough, state
   clearly what additional information is needed — don't guess.
