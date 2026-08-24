#!/usr/bin/env python3
import json
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_keymodel_trace.py TRACE.jsonl")
rows = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").splitlines() if line]
keys = [row for row in rows if row.get("type") == "key"]
if len(keys) != 1 or keys[0].get("keysym") != "EuroSign":
    raise SystemExit(f"expected one decoded EuroSign keysym, got {keys}")
if not (keys[0].get("state", 0) & 0x80):
    raise SystemExit("AZERTY EuroSign arrived without X11 Mod5/AltGr state")
print(json.dumps({
    "schema": 1,
    "result": "pass",
    "layout": "azerty-altgr-fixture",
    "keysym": keys[0]["keysym"],
    "text": "€",
    "keycode": keys[0]["keycode"],
    "state": keys[0]["state"],
}, sort_keys=True))
