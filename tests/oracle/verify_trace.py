#!/usr/bin/env python3
"""Validate the independent input-oracle JSONL trace."""
from __future__ import annotations

import json
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_trace.py TRACE.jsonl")
path = pathlib.Path(sys.argv[1])
rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
if not rows or rows[0].get("type") != "ready" or rows[0].get("schema") != 1:
    raise SystemExit("trace has no schema-1 ready record")
events = [row for row in rows if row.get("type") == "key"]
if [row.get("phase") for row in events] != ["down", "up"]:
    raise SystemExit(f"expected down/up pair, got: {events}")
if len({row.get("keycode") for row in events}) != 1:
    raise SystemExit("press/release keycodes differ")
if not all(row.get("xtest") is True for row in events):
    raise SystemExit("XI2 sourceid did not classify the Send events as XTEST")
if events[1]["monotonic_us"] < events[0]["monotonic_us"]:
    raise SystemExit("monotonic event order is reversed")
summary = {
    "schema": 1,
    "result": "pass",
    "phases": [row["phase"] for row in events],
    "keycode": events[0]["keycode"],
    "sourceids": [row["sourceid"] for row in events],
    "xtest": [row["xtest"] for row in events],
    "delta_us": events[1]["monotonic_us"] - events[0]["monotonic_us"],
}
print(json.dumps(summary, sort_keys=True))
