## Purpose

<!-- 1-3 sentences: what changed, why. Link the issue: Closes #123 -->

## Main changes (by component)

- `component`: ...

## Compatibility impact

- [ ] None
- [ ] NVS schema (`APP_CONFIG_VERSION` bumped, has migration/falls back to safe defaults)
- [ ] Partition table (devices in the field **cannot** OTA — need a USB flash)
- [ ] Payload format / MQTT topic / LoRa fPort (server needs an update)
- [ ] Kconfig / `sdkconfig.defaults`
- [ ] Security: Secure Boot / Flash Encryption / eFuse (**not reversible** — has an ADR: `docs/adr/...`)

## Edge cases handled

<!-- timeout, connection loss, malformed data, called before init, power loss mid-operation... -->

## Testing

| Level | Result |
|---|---|
| `esp32c5` build, 0 warnings | ☐ |
| Host tests | ☐ |
| Bench (real board) – description | ☐ |
| OTA + rollback (if touching OTA/boot) | ☐ |
| Soak test (if touching tasks/memory/connectivity) – number of hours | ☐ |

**Not yet verified on hardware:** <!-- state clearly, or "None" -->

## Checklist

- [ ] Commit message follows the rules (`tools/hooks/commit-msg` passes)
- [ ] Self-reviewed against `.github/instructions/review.instructions.md`
- [ ] `docs/` updated if architecture/dataflow changed
- [ ] No secrets, `sdkconfig`, or build files in the diff
