---
name: add-message-type
description: 'Add a new uplink message type (device → server) or downlink command (server → device) following the transport layer''s table-driven architecture, for both WiFi/MQTT and LoRaWAN. Use when new data needs to be sent, or a new command from the server needs to be supported.'
argument-hint: '<uplink|downlink> <TYPE_NAME> <data description> [mqtt|lorawan|both]'
---

# Add a message type / command

Read `components/transport/include/transport.h`, `transport_wifi_mqtt.c`, `transport_lorawan.c`,
`main/main.c`, and [docs/dataflow.md](../../../docs/dataflow.md) before making any changes.

## Uplink (APP_MSG_*)

1. `transport.h`: add `APP_MSG_<X>` to `app_msg_type_t`; if it carries data → add a branch to the
   `app_message_t.data` union (a fixed-size type, with the unit in its name).
2. For each supported transport: write its own `encode_<x>_payload()` and add one row to
   `s_publish_routes[]`.
   - MQTT: compact JSON, with `"v"` (schema version), `"ts"`, `"seq"`; check `snprintf` for
     truncation.
   - LoRaWAN: fixed-point binary, big-endian, pick a dedicated fPort, **size ≤ the max payload of the
     lowest DR in use** — look this up in the *LoRaWAN Regional Parameters* table for AS923 (note
     that a 400 ms dwell time reduces the max payload), don't rely on memory for the number. If it's
     exceeded, tell the user.
   - Transport doesn't support it: return `ESP_ERR_NOT_SUPPORTED` (never crash).
3. `main.c`: add a collector and a row in the corresponding transport's schedule table (the LoRaWAN
   interval must respect the duty cycle — write `TODO(confirm)` if airtime hasn't been calculated
   yet).

## Downlink (APP_CMD_*)

1. `transport.h`: add `APP_CMD_<X>` + the decoded data to the `app_command_t` union.
2. Decoder in each transport: **validate** length, range, version; an invalid command →
   `APP_CMD_UNKNOWN` + log a WARN.
3. `main.c::on_downlink_command()`: add a case; don't block inside the callback — hand off to a task
   if the work takes a while.
4. Commands that change configuration/have a physical effect: check system state, guard against
   replay/duplicates (seq/id), and send an ACK response.

## Wrap-up

- Add host tests for the encode/decode (boundaries, malformed input, truncation).
- Update section 9 "Message formats" in `docs/dataflow.md`. Changing an existing message format is a
  `BREAKING CHANGE`.
