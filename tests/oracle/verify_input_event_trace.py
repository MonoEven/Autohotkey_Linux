#!/usr/bin/env python3
from __future__ import annotations
import json
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_input_event_trace.py TRACE.jsonl")
rows = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").splitlines() if line]
if not rows or any(row.get("schema") != 1 for row in rows):
    raise SystemExit("missing or non-v1 normalized input events")

def pair(vk: int, source: str):
    events = [r for r in rows if r.get("vk") == vk and r.get("source") == source]
    phases = [r.get("release") for r in events]
    if phases != [False, True]:
        raise SystemExit(f"{source} vk={vk}: expected down/up, got {events}")
    if any(r.get("origin") != "x11" for r in events):
        raise SystemExit(f"{source} vk={vk}: wrong origin")
    return events

self_events = pair(65, "self_inject")
other_events = pair(66, "other_inject")
if any(r.get("send_level") != 3 for r in self_events):
    raise SystemExit(f"self SendLevel lost: {self_events}")
if any(r.get("send_level") != -1 for r in other_events):
    raise SystemExit(f"external injection got forged SendLevel: {other_events}")
if self_events[0].get("sc") != 0x1E or self_events[0].get("evdev_code") != 30:
    raise SystemExit(f"self physical layers wrong: {self_events[0]}")
if other_events[0].get("sc") != 0x30 or other_events[0].get("evdev_code") != 48:
    raise SystemExit(f"external physical layers wrong: {other_events[0]}")
if self_events[0].get("text") != ord("a") or other_events[0].get("text") != ord("b"):
    raise SystemExit("text layer mismatch")
if any(rows[i]["timestamp_us"] > rows[i + 1]["timestamp_us"] for i in range(len(rows) - 1)):
    raise SystemExit("normalized timestamps are not monotonic")
print(json.dumps({
    "schema": 1, "result": "pass", "events": len(rows),
    "self": {"vk": 65, "sc": 30, "evdev": 30, "level": 3},
    "other": {"vk": 66, "sc": 48, "evdev": 48, "level": -1},
    "origins": sorted(set(r["origin"] for r in rows)),
}, sort_keys=True))
