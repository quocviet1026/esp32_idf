---
name: 'Commit message rules'
description: 'Rules for writing git commit messages for the team (Conventional Commits + scope per component). Use when generating a commit message, creating a commit, or writing a PR description.'
---

# Commit message rules

Standard: **Conventional Commits 1.0**, written **in English**, concise but **listing every change**
in the commit. The `tools/hooks/commit-msg` hook automatically checks against the rules below.

## Format

```
<type>(<scope>): <subject>
<blank line>
- <change 1>
- <change 2>
<blank line>
<footer>
```

### Header (required)

- **Max 72 characters**, no trailing period, verb in the **imperative present** mood, lowercase
  first letter: `add`, `fix`, `handle`, `remove` (not `added`, `fixes`).
- `type` – pick exactly one:

| type | When |
|---|---|
| `feat` | A new feature for the user/server |
| `fix` | A bug fix |
| `perf` | Performance/power improvement, no behavior change |
| `refactor` | Restructuring, no behavior change |
| `sec` | A security fix or hardening |
| `test` | Adding/fixing tests |
| `docs` | Documentation only |
| `build` | CMake, Kconfig, `sdkconfig.defaults`, partitions, dependencies (`idf_component.yml`) |
| `ci` | CI pipeline |
| `style` | Formatting only, no logic change |
| `chore` | Other miscellaneous work (scripts, .gitignore) |
| `revert` | Reverting a previous commit |

- `scope` – the **main component** affected (required, except for `docs`/`ci`/`chore`):
  `main`, `transport`, `mqtt`, `wifi`, `lorawan`, `rak3172`, `ota`, `config`, `cli`, `display`, `ui`,
  `button`, `sensor`, `storage`, `health`, `bsp`, `partition`, `deps`.
  Multiple components: pick the component that **bears the main change**; if truly equal, use up to
  2 scopes separated by a comma `(mqtt,ota)`. More than 2 → **split into separate commits**.

### Body (required if the commit has more than 1 change or > 20 diff lines)

- Each significant change is **one bullet** `- `, starting with an imperative verb, **max 72
  characters per line**.
- State **what and why**, not the implementation detail (that's already in the diff).
- Note any edge case/error behavior newly handled.
- Note any change affecting compatibility: NVS schema, partitions, Kconfig, MQTT topic, payload
  format, LoRa port.

### Footer

- `BREAKING CHANGE: <description>` – when changing the payload format, topic, NVS schema,
  partitions, or a public API. Also add `!` after the type/scope: `feat(config)!: ...`.
- `Refs: #123` or `Closes: #123` – links an issue.
- `Tested: <hw|bench|build|none> - <short description>` – **required** for `feat`, `fix`, `perf`,
  `sec`. **Why:** a passing build doesn't mean the firmware runs correctly on hardware; the reviewer
  needs to know how far it's actually been verified.

## Principles

- **1 commit = 1 logical purpose.** Don't mix refactor + feat + formatting in one commit.
- Never commit: `sdkconfig`, `build/`, `managed_components/`, secrets, private certs, `.bin` files.
- When generating a message from a diff: read the **entire staged diff**, group changes by purpose,
  and don't miss any change in Kconfig/CMake/partitions/docs.

## Examples

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

**Bad** examples: `update code`, `fix bug`, `WIP`, `feat: many changes`, `Fixed the mqtt reconnect issue.`
