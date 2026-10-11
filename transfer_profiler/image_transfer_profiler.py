#!/usr/bin/env python3
"""
Image transfer profiler: measures every image transfer session end to end.

Data sources (all on the same MQTT broker + the ESP32 serial log):
  * MQTT  <dev_id>/Command      Backend -> Bridge     (session start)
  * MQTT  downlink/<devaddr>    Bridge  -> Gateway    (command + chunk downlinks)
  * MQTT  uplink/<...>          Gateway -> Bridge     (ImageRequest / chunk request / Handshake)
          The gateway adds a timestamp field to its messages (--ts-field).
  * Serial (COM port) of the ESP32: the device has no RTC, so each log line is
    stamped with the PC clock on arrival. Two regex patterns select the lines:
      --req-pattern   "device requests chunk"
      --recv-pattern  "device received chunk"
    A named group (?P<idx>\\d+) (or the first group) extracts the chunk index.

Settings live in profiler.ini (next to this file, or --config PATH); command-line flags override it.

One JSON report per session is written to --out-dir. A session starts at a Backend
"Image" command (or at a device ImageRequest without ChunkIndex) and ends when all
chunks are received (+ --grace), the device reports Handshake Complete/Failed,
--idle-timeout expires, or a new session starts.

Per chunk (last, successful attempt) the time is split into:
  dev_to_gw_ms     serial "request" log -> gateway timestamp of the uplink
  gw_to_broker_ms  gateway timestamp -> arrival at this tool (needs synced clocks)
  bridge_ms        uplink arrival -> chunk downlink published by the Bridge
  down_ms          chunk downlink published -> serial "received" log
                   (broker -> gateway -> Class A RX window -> RF -> module -> ESP32)
  total_ms         serial request -> serial received (single attempt)
and retries / losses are counted from how many times each stage was seen.

Example:
  python image_transfer_profiler.py --broker localhost --dev-id T3CDC75810B64 \\
      --deveui AC1F09FFFE1E49B2 --serial-port COM5 \\
      --req-pattern "Request chunk (?P<idx>\\d+)" --recv-pattern "Got chunk (?P<idx>\\d+)"
"""

import argparse
import configparser
import json
import math
import queue
import re
import statistics
import sys
import threading
import time
import uuid
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

# Reuse the bridge's TLV parser so the tool decodes exactly what the bridge does.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "lorawan_bridge_services" / "src"))
from protocol.tlv import TLVCommand, TLVTopicName, TLVType, parse_tlv  # noqa: E402

ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")   # ESP-IDF colours its log lines
ATTEMPT_TOLERANCE_S = 1.0   # serial request log may arrive slightly after the gateway uplink
RX1_RISK_MS = 800           # LoRaWAN Class A RX1 opens 1 s after the uplink


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def parse_ts(value):
    """Gateway timestamp -> epoch seconds (float). Accepts s/ms/us/ns numbers or ISO-8601."""
    if value is None:
        return None
    if isinstance(value, (int, float)):
        v = float(value)
        for limit, div in ((1e17, 1e9), (1e14, 1e6), (1e11, 1e3)):
            if abs(v) >= limit:
                return v / div
        return v
    if isinstance(value, str):
        s = value.strip()
        try:
            return parse_ts(float(s))
        except ValueError:
            pass
        try:
            dt = datetime.fromisoformat(re.sub(r"(\.\d{6})\d+", r"\1", s.replace("Z", "+00:00")))
        except ValueError:
            return None
        return (dt if dt.tzinfo else dt.replace(tzinfo=timezone.utc)).timestamp()
    return None


def get_path(obj, dotted):
    for key in dotted.split("."):
        if not isinstance(obj, dict):
            return None
        obj = obj.get(key)
    return obj


def ms(a, b):
    return None if a is None or b is None else round((b - a) * 1000.0, 1)


def pct(values, p):
    s = sorted(values)
    return s[max(0, min(len(s) - 1, math.ceil(p / 100.0 * len(s)) - 1))]


def stats(values):
    values = [v for v in values if v is not None]
    if not values:
        return None
    return {
        "n": len(values),
        "min": round(min(values), 1),
        "mean": round(statistics.fmean(values), 1),
        "p50": round(pct(values, 50), 1),
        "p95": round(pct(values, 95), 1),
        "max": round(max(values), 1),
    }


def iso(t):
    return None if t is None else datetime.fromtimestamp(t).astimezone().isoformat(timespec="milliseconds")


def parse_payload(hex_str):
    """Decode the TLV fields this tool cares about from a LoRaWAN payload (hex)."""
    f = {"topic": None, "cmd": None, "idx": None, "total": None, "chunk_len": None,
         "device_id": None, "status": None}
    try:
        raw = bytes.fromhex(hex_str)
    except (TypeError, ValueError):
        return f
    for t, v in parse_tlv(raw):
        if t == TLVTopicName.IMAGE_REQUEST:
            f["topic"] = "ImageRequest"
        elif t == TLVTopicName.HANDSHAKE:
            f["topic"] = "Handshake"
        elif t == TLVTopicName.COMMAND and v:
            f["cmd"] = v[0]
        elif t == TLVType.CHUNK_INDEX:
            f["idx"] = int.from_bytes(v, "big")
        elif t == TLVType.TOTAL_CHUNKS:
            f["total"] = int.from_bytes(v, "big")
        elif t == TLVType.CHUNK_DATA:
            f["chunk_len"] = len(v)
        elif t == TLVType.DEVICE_ID:
            f["device_id"] = v.decode("utf-8", errors="replace")
        elif t == TLVType.COMMAND_STATUS and v:
            f["status"] = {1: "Received", 2: "Complete", 3: "Failed"}.get(v[0], f"0x{v[0]:02X}")
    return f


# ---------------------------------------------------------------------------
# Session
# ---------------------------------------------------------------------------

class Session:
    def __init__(self, sid, t0, trigger, dev_id):
        self.id = sid
        self.t0 = t0
        self.trigger = trigger
        self.dev_id = dev_id
        self.last_activity = t0
        self.events = []
        self.cmd_t = None
        self.dl_cmd = []        # t_pc of command downlinks
        self.ul_init = []       # {"t_pc","t_gw"} of init ImageRequest uplinks
        self.handshakes = []    # {"t_pc","status"}
        self.total = None
        self.done_t = None      # when the end condition was first met
        self.done_reason = None
        # per chunk: ser_req/ser_recv/dl are lists of t_pc, ul is a list of {"t_pc","t_gw"}
        self.chunks = defaultdict(lambda: {"ser_req": [], "ul": [], "dl": [], "ser_recv": [], "len": None})

    def add(self, src, kind, t_pc, t_gw=None, idx=None, **extra):
        self.last_activity = t_pc
        self.events.append({"src": src, "kind": kind, "idx": idx, "t_pc": t_pc, "t_gw": t_gw, **extra})

    def has_chunk_traffic(self):
        return bool(self.chunks)


# ---------------------------------------------------------------------------
# Tracker: session state machine (no I/O except writing the report)
# ---------------------------------------------------------------------------

class Tracker:
    def __init__(self, args):
        self.args = args
        self.serial_on = bool(args.serial_port)
        self.req_re = re.compile(args.req_pattern) if args.req_pattern else None
        self.recv_re = re.compile(args.recv_pattern) if args.recv_pattern else None
        self.offset_s = args.gw_offset_ms / 1000.0
        self.cur = None
        # A configured devaddr is a strict filter; otherwise devaddrs are learned from uplinks/commands.
        self.devaddr_fixed = bool(args.devaddr)
        self.devaddrs = {args.devaddr.upper()} if args.devaddr else set()
        self.seq = 0
        self.last_report = None

    # --- session lifecycle -------------------------------------------------

    def _start(self, trigger, t, dev_id=None):
        self.seq += 1
        self.cur = Session(self.seq, t, trigger, dev_id or self.args.dev_id)
        print(f"[session {self.cur.id}] started ({trigger}) at {iso(t)}")
        return self.cur

    def _ensure(self, trigger, t):
        return self.cur or self._start(trigger, t)

    def _finish(self, reason):
        s, self.cur = self.cur, None
        if s is None:
            return
        report = analyze(s, reason, self.serial_on, self.args)
        out = Path(self.args.out_dir)
        out.mkdir(parents=True, exist_ok=True)
        stamp = datetime.fromtimestamp(s.t0).strftime("%Y%m%d_%H%M%S")
        who = (s.dev_id or self.args.deveui or "unknown").replace("/", "_")
        path = out / f"session_{stamp}_{who}_{s.id}.json"
        path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        report["_path"] = str(path)
        self.last_report = report
        print(format_summary(report, path))

    def finish_current(self, reason):
        self._finish(reason)

    def tick(self, now):
        s = self.cur
        if s is None:
            return
        covered = {i for i, c in s.chunks.items() if (c["ser_recv"] if self.serial_on else c["dl"])}
        if s.done_t is None:
            if s.total is not None and len(covered) >= s.total:
                s.done_t, s.done_reason = now, "complete"
            elif s.handshakes and s.handshakes[-1]["status"] in ("Complete", "Failed"):
                s.done_t = now
                s.done_reason = "complete" if s.handshakes[-1]["status"] == "Complete" else "device-failed"
        if s.done_t is not None and now - s.done_t >= self.args.grace:
            self._finish(s.done_reason)
        elif now - s.last_activity >= self.args.idle_timeout:
            self._finish("timeout")

    # --- MQTT --------------------------------------------------------------

    def on_mqtt(self, topic, payload, t_pc):
        try:
            msg = json.loads(payload)
        except (ValueError, UnicodeDecodeError):
            return
        if not isinstance(msg, dict):
            return
        parts = topic.split("/")
        if parts[0] == "uplink":
            self._uplink(msg, t_pc)
        elif parts[0] == "downlink":
            self._downlink(msg, parts, t_pc)
        elif len(parts) == 2 and parts[1].lower() == "command":
            self._command(parts[0], msg, t_pc)

    def _command(self, dev_id, msg, t_pc):
        if self.args.dev_id and dev_id != self.args.dev_id:
            return
        if str(msg.get("Command", "")).strip().lower() != "image":
            return
        if self.cur:
            self._finish("superseded" if self.cur.done_t is None else self.cur.done_reason)
        s = self._start("backend-command", t_pc, dev_id)
        s.cmd_t = t_pc
        s.add("mqtt", "backend_command", t_pc, message_id=msg.get("MessageID"),
              image=msg.get("ImageURL"), w=msg.get("ImageWidth"), h=msg.get("ImageHeight"))

    def _uplink(self, msg, t_pc):
        deveui = str(msg.get("deveui") or "").upper()
        if self.args.deveui and deveui != self.args.deveui.upper():
            return
        if self.devaddr_fixed and str(msg.get("devaddr") or "").upper() not in self.devaddrs:
            return
        if msg.get("devaddr") and not self.devaddr_fixed:
            self.devaddrs.add(str(msg["devaddr"]).upper())
        t_gw = parse_ts(get_path(msg, self.args.ts_field))
        if t_gw is not None:
            t_gw += self.offset_s
        f = parse_payload(msg.get("data"))

        if f["topic"] == "ImageRequest" and f["idx"] is None:
            s = self.cur
            if s is None or s.has_chunk_traffic():
                if s:
                    self._finish("superseded" if s.done_t is None else s.done_reason)
                s = self._start("device-init-request", t_pc)
            s.ul_init.append({"t_pc": t_pc, "t_gw": t_gw})
            s.add("mqtt", "uplink_image_init", t_pc, t_gw)
        elif f["topic"] == "ImageRequest":
            s = self._ensure("mid-transfer", t_pc)
            s.chunks[f["idx"]]["ul"].append({"t_pc": t_pc, "t_gw": t_gw})
            s.add("mqtt", "uplink_chunk_request", t_pc, t_gw, f["idx"])
        elif f["topic"] == "Handshake" and self.cur:
            self.cur.handshakes.append({"t_pc": t_pc, "status": f["status"]})
            self.cur.add("mqtt", "uplink_handshake", t_pc, t_gw, status=f["status"])

    def _downlink(self, msg, parts, t_pc):
        devaddr = str(msg.get("devaddr") or (parts[1] if len(parts) > 1 else "")).upper()
        f = parse_payload(msg.get("data"))
        filtered = bool(self.args.deveui or self.args.dev_id or self.args.devaddr)

        if f["cmd"] == int(TLVCommand.IMAGE):
            if self.args.dev_id and f["device_id"] != self.args.dev_id:
                return
            if self.devaddr_fixed and devaddr not in self.devaddrs:
                return
            self.devaddrs.add(devaddr)
            s = self.cur
            if s is None or s.has_chunk_traffic():
                if s:
                    self._finish("superseded" if s.done_t is None else s.done_reason)
                s = self._start("downlink-command", t_pc, f["device_id"])
            s.dl_cmd.append(t_pc)
            s.add("mqtt", "downlink_image_command", t_pc)
        elif f["idx"] is not None and self.cur:
            if filtered and devaddr not in self.devaddrs:
                return
            s = self.cur
            c = s.chunks[f["idx"]]
            c["dl"].append(t_pc)
            c["len"] = f["chunk_len"] or c["len"]
            if f["total"] is not None:
                s.total = f["total"]
            s.add("mqtt", "downlink_chunk", t_pc, idx=f["idx"], bytes=f["chunk_len"], total=f["total"])

    # --- serial ------------------------------------------------------------

    def _serial_idx(self, m, kind):
        try:
            if "idx" in m.re.groupindex:
                return int(m.group("idx")), False
            if m.re.groups >= 1:
                return int(m.group(1)), False
        except (TypeError, ValueError):
            pass
        s = self.cur
        if kind == "recv":      # pair with the most recent request
            last = next((e["idx"] for e in reversed(s.events) if e["kind"] == "serial_chunk_request"), None)
            return last, True
        got = sum(1 for c in s.chunks.values() if c["ser_recv"])    # next chunk after those received
        return got, True

    def on_serial(self, line, t_pc):
        s = self.cur
        if s is None:
            return
        m = self.recv_re.search(line) if self.recv_re else None
        if m:
            idx, inferred = self._serial_idx(m, "recv")
            if idx is not None:
                s.chunks[idx]["ser_recv"].append(t_pc)
                s.add("serial", "serial_chunk_received", t_pc, idx=idx, idx_inferred=inferred, line=line[:200])
            return
        m = self.req_re.search(line) if self.req_re else None
        if m:
            idx, inferred = self._serial_idx(m, "req")
            if idx is not None:
                s.chunks[idx]["ser_req"].append(t_pc)
                s.add("serial", "serial_chunk_request", t_pc, idx=idx, idx_inferred=inferred, line=line[:200])


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------

def analyze(s, reason, serial_on, args):
    chunk_rows = []
    anomalies = []
    negatives = 0

    for idx in sorted(s.chunks):
        c = s.chunks[idx]
        sr, ul, dl, rv = c["ser_req"], c["ul"], c["dl"], c["ser_recv"]
        first_req = sr[0] if sr else (ul[0]["t_pc"] if ul else None)
        first_rv = rv[0] if rv else None

        dl_ok = max((t for t in dl if first_rv is None or t <= first_rv), default=None) if first_rv else None
        ul_ok = max((u for u in ul if dl_ok is not None and u["t_pc"] <= dl_ok), key=lambda u: u["t_pc"], default=None)
        sr_ok = max((t for t in sr if ul_ok is not None and t <= ul_ok["t_pc"] + ATTEMPT_TOLERANCE_S), default=None)

        t_gw = ul_ok["t_gw"] if ul_ok else None
        row = {
            "idx": idx,
            "bytes": c["len"],
            "device_requests": len(sr),
            "gateway_uplinks": len(ul),
            "bridge_downlinks": len(dl),
            "device_received": len(rv),
            "retries": max(0, (len(sr) or len(ul)) - 1),
            "lost_uplink": max(0, len(sr) - len(ul)) if serial_on else None,
            "no_bridge_reply": max(0, len(ul) - len(dl)),
            "lost_downlink": max(0, len(dl) - len(rv)) if serial_on else None,
            "duplicate_received": max(0, len(rv) - 1),
            "dev_to_gw_ms": ms(sr_ok, t_gw),
            "gw_to_broker_ms": ms(t_gw, ul_ok["t_pc"]) if ul_ok else None,
            "uplink_total_ms": ms(sr_ok, ul_ok["t_pc"]) if ul_ok else None,
            "bridge_ms": ms(ul_ok["t_pc"], dl_ok) if ul_ok else None,
            "down_ms": ms(dl_ok, first_rv),
            "total_ms": ms(sr_ok, first_rv),
            "total_with_retries_ms": ms(first_req, first_rv),
        }
        if serial_on:
            row["status"] = "missing" if not rv else ("ok_after_retry" if row["retries"] else "ok")
        else:
            row["status"] = "sent" if dl else "no_downlink"
        slow = []       # every uplink->reply pair (not only the successful one) that overran RX1
        for t in dl:
            prev = max((u for u in ul if u["t_pc"] <= t), key=lambda u: u["t_pc"], default=None)
            if prev:
                lag = (t - prev["t_pc"]) * 1000 + (ms(prev["t_gw"], prev["t_pc"]) or 0)
                if lag > RX1_RISK_MS:
                    slow.append(round(lag, 1))
        if slow:
            row["rx1_risk_ms"] = slow
        for key in ("dev_to_gw_ms", "gw_to_broker_ms", "uplink_total_ms", "bridge_ms", "down_ms", "total_ms"):
            if row[key] is not None and row[key] < -50:
                negatives += 1
        chunk_rows.append(row)

        if row["status"] in ("missing", "no_downlink"):
            anomalies.append(f"chunk {idx}: never received by device")
        if row["lost_uplink"]:
            anomalies.append(f"chunk {idx}: {row['lost_uplink']} uplink request(s) not seen at gateway")
        if row["no_bridge_reply"]:
            anomalies.append(f"chunk {idx}: {row['no_bridge_reply']} uplink(s) got no downlink from bridge")
        if row["lost_downlink"]:
            anomalies.append(f"chunk {idx}: {row['lost_downlink']} downlink(s) sent but not received (RX window missed / RF loss)")
        if row.get("rx1_risk_ms"):
            anomalies.append(f"chunk {idx}: gateway->bridge->reply took {row['rx1_risk_ms']} ms (>{RX1_RISK_MS}), may miss RX1")

    if s.total is not None:
        for idx in range(s.total):
            if idx not in s.chunks:
                chunk_rows.append({"idx": idx, "status": "never_requested"})
                anomalies.append(f"chunk {idx}: never requested")
        chunk_rows.sort(key=lambda r: r["idx"])

    # skew hint between gateway clock and this PC (only meaningful when ts is present)
    g2b = [r["gw_to_broker_ms"] for r in chunk_rows if r.get("gw_to_broker_ms") is not None]
    skew = round(statistics.median(g2b), 1) if g2b else None
    if negatives or (skew is not None and abs(skew) > 1000):
        anomalies.append(f"clock skew suspected (median gw_to_broker={skew} ms, {negatives} negative stage(s)); "
                         "sync the clocks or use --gw-offset-ms")

    # session-level stages (all on this tool's clock: broker arrival / serial arrival)
    first_chunk_req = min((r for r in ([c["ser_req"][0] for c in s.chunks.values() if c["ser_req"]] +
                                       [c["ul"][0]["t_pc"] for c in s.chunks.values() if c["ul"]])), default=None)
    last_rv = max((c["ser_recv"][-1] for c in s.chunks.values() if c["ser_recv"]), default=None)
    last_dl = max((t for c in s.chunks.values() for t in c["dl"]), default=None)
    t_end = last_rv or last_dl or s.last_activity
    t_start = s.cmd_t or (s.dl_cmd[0] if s.dl_cmd else None) or (s.ul_init[0]["t_pc"] if s.ul_init else s.t0)
    t_init = s.ul_init[0]["t_pc"] if s.ul_init else None
    t_dlcmd = s.dl_cmd[0] if s.dl_cmd else None

    distinct = {i: c["len"] for i, c in s.chunks.items() if c["len"] and c["dl"]}
    comp_bytes = sum(distinct.values())
    chunk_phase_s = (t_end - first_chunk_req) if first_chunk_req else None

    sent_all = sum(1 for r in chunk_rows if r.get("status") in ("ok", "ok_after_retry", "sent"))
    summary = {
        "result": reason,
        "total_chunks": s.total,
        "chunks_ok": sent_all,
        "chunks_missing": [r["idx"] for r in chunk_rows if r.get("status") in ("missing", "no_downlink", "never_requested")],
        "stages_ms": {
            "backend_cmd_to_bridge_downlink": ms(s.cmd_t, t_dlcmd),
            "command_delivery_to_device_init_request": ms(t_dlcmd, t_init),
            "init_request_to_first_chunk_request": ms(t_init, first_chunk_req),
            "chunk_phase": ms(first_chunk_req, t_end),
            "total": ms(t_start, t_end),
        },
        "retries": {
            "init_request_repeats": max(0, len(s.ul_init) - 1),
            "command_downlink_repeats": max(0, len(s.dl_cmd) - 1),
            "chunk_retries_total": sum(r.get("retries", 0) for r in chunk_rows),
            "chunks_with_retry": [r["idx"] for r in chunk_rows if r.get("retries")],
            "lost_uplink_total": sum(r.get("lost_uplink") or 0 for r in chunk_rows),
            "no_bridge_reply_total": sum(r.get("no_bridge_reply") or 0 for r in chunk_rows),
            "lost_downlink_total": sum(r.get("lost_downlink") or 0 for r in chunk_rows),
            "duplicate_received_total": sum(r.get("duplicate_received") or 0 for r in chunk_rows),
        },
        "per_chunk_stats_ms": {k: stats([r.get(k) for r in chunk_rows]) for k in (
            "total_ms", "total_with_retries_ms", "dev_to_gw_ms", "gw_to_broker_ms", "bridge_ms", "down_ms")},
        "compressed_bytes": comp_bytes or None,
        "goodput_bytes_per_s": round(comp_bytes / chunk_phase_s, 1) if comp_bytes and chunk_phase_s else None,
        "gateway_clock_skew_hint_ms": skew,
    }
    if not s.ul_init:
        anomalies.append("no init ImageRequest uplink seen")
    if s.total is None:
        anomalies.append("TOTAL_CHUNKS unknown (no chunk downlink seen)")

    t0 = s.t0
    events = [{"rel_ms": round((e["t_pc"] - t0) * 1000, 1), "t_pc": iso(e["t_pc"]),
               "t_gw": iso(e["t_gw"]), **{k: v for k, v in e.items() if k not in ("t_pc", "t_gw")}}
              for e in s.events]
    return {
        "session": {"id": s.id, "trigger": s.trigger, "dev_id": s.dev_id, "start": iso(s.t0),
                    "end": iso(t_end), "result": reason},
        "config": {k: v for k, v in vars(args).items() if k not in ("password",)},
        "summary": summary,
        "anomalies": anomalies,
        "chunks": chunk_rows,
        "events": events,
    }


def format_summary(r, path):
    sm, st = r["summary"], r["summary"]["stages_ms"]
    lines = [
        f"[session {r['session']['id']}] {sm['result'].upper()}  chunks ok {sm['chunks_ok']}/{sm['total_chunks']}"
        f"  total {fmt_ms(st['total'])}  -> {path}",
        f"    stages: backend->bridge {fmt_ms(st['backend_cmd_to_bridge_downlink'])} | "
        f"cmd->device init {fmt_ms(st['command_delivery_to_device_init_request'])} | "
        f"init->1st chunk req {fmt_ms(st['init_request_to_first_chunk_request'])} | "
        f"chunk phase {fmt_ms(st['chunk_phase'])}",
    ]
    for key, label in (("total_ms", "chunk total"), ("dev_to_gw_ms", "dev->gw"), ("bridge_ms", "bridge"), ("down_ms", "down")):
        v = sm["per_chunk_stats_ms"][key]
        if v:
            lines.append(f"    {label:<12} mean {v['mean']} p50 {v['p50']} p95 {v['p95']} max {v['max']} ms")
    rt = sm["retries"]
    lines.append(f"    retries {rt['chunk_retries_total']} | lost UL {rt['lost_uplink_total']} | "
                 f"no bridge reply {rt['no_bridge_reply_total']} | lost DL {rt['lost_downlink_total']} | "
                 f"missing {sm['chunks_missing']}")
    for a in r["anomalies"][:10]:
        lines.append(f"    ! {a}")
    if len(r["anomalies"]) > 10:
        lines.append(f"    ! ... {len(r['anomalies']) - 10} more in the report")
    return "\n".join(lines)


def fmt_ms(v):
    return "n/a" if v is None else f"{v:.0f} ms"


# ---------------------------------------------------------------------------
# I/O
# ---------------------------------------------------------------------------

def start_mqtt(args, q):
    import paho.mqtt.client as mqtt
    # The broker allows one connection per client id, so several machines need distinct ids.
    client_id = args.client_id or f"profiler-{args.dev_id or 'any'}-{uuid.uuid4().hex[:6]}"
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    except AttributeError:      # paho-mqtt 1.x
        client = mqtt.Client(client_id=client_id)
    if args.username:
        client.username_pw_set(args.username, args.password)

    def on_connect(c, userdata, flags, rc, props=None):
        print(f"MQTT connected to {args.broker}:{args.port} as {client_id} (rc={rc})")
        for topic in ("uplink/#", "downlink/#", "+/Command", "+/command"):
            c.subscribe(topic)

    def on_message(c, userdata, msg):
        q.put(("mqtt", msg.topic, msg.payload, time.time()))

    client.on_connect, client.on_message = on_connect, on_message
    client.connect(args.broker, args.port, 60)
    client.loop_start()
    return client


def open_serial(args):
    import serial
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = args.serial_port, args.baud, 0.1
    ser.dtr = ser.rts = False       # opening the port must not reset the ESP32
    ser.open()
    print(f"Serial opened: {args.serial_port} @ {args.baud}")
    return ser


def serial_reader(ser, q, stop, echo=False, raw_log=None):
    """Read the port line by line. Optionally act as a log monitor: echo to console and/or save every line."""
    buf = b""
    log = open(raw_log, "a", encoding="utf-8") if raw_log else None
    with ser:
        while not stop.is_set():
            data = ser.read(ser.in_waiting or 1)
            if not data:
                continue
            now = time.time()
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = ANSI_RE.sub("", line.decode("utf-8", errors="replace").rstrip("\r"))
                stamp = datetime.fromtimestamp(now).strftime("%H:%M:%S.%f")[:-3]
                if echo:
                    print(f"{stamp} | {line}", flush=True)
                if log:
                    log.write(f"{datetime.fromtimestamp(now).isoformat(timespec='milliseconds')} {line}\n")
                    log.flush()
                q.put(("serial", line, now))
    if log:
        log.close()


def load_config(path):
    """Flatten an INI file (sections are only for readability) into {arg_dest: value}; empty values are skipped."""
    cp = configparser.ConfigParser(interpolation=None)     # no % interpolation: values hold regexes
    if not cp.read(path, encoding="utf-8"):
        raise SystemExit(f"cannot read config file: {path}")
    return {k.replace("-", "_"): v for sec in cp.sections() for k, v in cp.items(sec) if v.strip()}


def parse_args(argv=None):
    pre = argparse.ArgumentParser(add_help=False)
    pre.add_argument("--config", default=str(Path(__file__).resolve().parent / "profiler.ini"))
    known, _ = pre.parse_known_args(argv)

    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], parents=[pre])
    p.add_argument("--broker", default="localhost")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--client-id", help="MQTT client id (default: unique per run, so many profilers can share a broker)")
    p.add_argument("--username")
    p.add_argument("--password")
    p.add_argument("--dev-id", help="DeviceID of the tag (filters <dev_id>/Command and downlinks)")
    p.add_argument("--deveui", help="DevEUI of the tag (filters uplinks)")
    p.add_argument("--devaddr", help="DevAddr of the tag; if set it is a strict filter for uplinks and downlinks "
                                     "(leave unset to learn it, e.g. with OTAA)")
    p.add_argument("--ts-field", default="timestamp", help="gateway timestamp field, dotted path allowed (default: timestamp)")
    p.add_argument("--gw-offset-ms", type=float, default=0.0, help="added to gateway timestamps to align with this PC's clock")
    p.add_argument("--serial-port", help="ESP32 log port, e.g. COM5 or /dev/ttyUSB0")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--req-pattern", help="regex of the ESP32 log line when it requests a chunk")
    p.add_argument("--recv-pattern", help="regex of the ESP32 log line when it receives a chunk")
    p.add_argument("--echo", nargs="?", const=True, default=False,
                   type=lambda v: v if isinstance(v, bool) else v.strip().lower() in ("1", "true", "yes", "on"),
                   help="print every serial line to the console (use instead of idf.py monitor)")
    p.add_argument("--raw-log", help="append every serial line, with PC timestamp, to this file")
    p.add_argument("--out-dir", default="transfer_logs")
    p.add_argument("--idle-timeout", type=float, default=120.0, help="seconds without events before a session is closed")
    p.add_argument("--grace", type=float, default=5.0, help="seconds to keep a finished session open for late events")
    if Path(known.config).exists():
        cfg = load_config(known.config)
        unknown = set(cfg) - {a.dest for a in p._actions}
        if unknown:
            p.error(f"unknown key(s) in {known.config}: {', '.join(sorted(unknown))}")
        p.set_defaults(**cfg)      # string defaults go through each option's type; CLI flags still win
    args = p.parse_args(argv)
    if args.serial_port and not (args.req_pattern and args.recv_pattern):
        p.error("--serial-port needs both --req-pattern and --recv-pattern")
    return args


def main():
    args = parse_args()
    if not (args.dev_id or args.deveui or args.devaddr):
        print("WARNING: no --dev-id/--deveui/--devaddr: traffic of all devices is merged into one session")
    tracker, q, stop = Tracker(args), queue.Queue(), threading.Event()
    try:
        client = start_mqtt(args, q)
    except OSError as e:
        sys.exit(f"cannot connect to MQTT broker {args.broker}:{args.port}: {e}")
    if args.serial_port:
        try:
            ser = open_serial(args)     # opened here so a wrong port fails immediately, not silently in a thread
        except Exception as e:
            client.loop_stop()
            sys.exit(f"cannot open serial port {args.serial_port}: {e}")
        threading.Thread(target=serial_reader, args=(ser, q, stop, args.echo, args.raw_log), daemon=True).start()
    print("Waiting for image transfers... (Ctrl+C to stop)")
    try:
        while True:
            try:
                item = q.get(timeout=0.5)
            except queue.Empty:
                item = None
            if item:
                if item[0] == "mqtt":
                    tracker.on_mqtt(item[1], item[2], item[3])
                else:
                    tracker.on_serial(item[1], item[2])
            tracker.tick(time.time())
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        client.loop_stop()
        tracker.finish_current("interrupted")


if __name__ == "__main__":
    main()
