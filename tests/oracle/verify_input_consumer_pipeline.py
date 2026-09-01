#!/usr/bin/env python3
from __future__ import annotations
import json
import pathlib
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: verify_input_consumer_pipeline.py WORK SUMMARY")
work = pathlib.Path(sys.argv[1])
summary = pathlib.Path(sys.argv[2])

def rows(name: str):
    return [json.loads(x) for x in (work / name).read_text(encoding="utf-8").splitlines() if x]

def fields(name: str):
    text = (work / name).read_text(encoding="utf-8").strip()
    return dict(p.split("=", 1) for p in text.split() if "=" in p), text

report = {}
for mode in ("active", "mirror", "legacy"):
    f, text = fields(f"x11-{mode}.out")
    want = {"kd":"1", "ku":"1", "chars":"v", "selected":"1", "eq":"0", "gt":"1", "gtlevel":"5", "mode":mode}
    if any(f.get(k) != v for k, v in want.items()):
        raise SystemExit(f"x11/{mode}: result mismatch {text}")
    rr = rows(f"x11-{mode}.trace")
    if not rr or any(r.get("domain") not in ("x11_raw", "x11_grab") for r in rr):
        raise SystemExit(f"x11/{mode}: missing/wrong trace")
    decisions = [r for r in rr if r.get("stage") == "consumer_decision"]
    outcomes = [r for r in rr if r.get("stage") == "consumer_outcome"]
    if not any(r.get("consumer") == "inputhook" and r.get("consumer_action") == "ignored"
               and r.get("consumer_reason") == "level_filtered" and r.get("send_level") == 4 for r in decisions):
        raise SystemExit(f"x11/{mode}: missing InputHook level filter")
    if not any(r.get("consumer") == "inputhook" and r.get("consumer_action") == "callback_queued"
               and r.get("consumer_reason") == "key_callback" and r.get("send_level") == 5 for r in decisions):
        raise SystemExit(f"x11/{mode}: missing key callback decision")
    if not any(r.get("consumer") == "inputhook" and r.get("outcome") == "callback_dispatched" for r in outcomes):
        raise SystemExit(f"x11/{mode}: missing callback outcome")
    if not any(r.get("consumer") == "hotstring" and r.get("consumer_action") == "ignored"
               and r.get("consumer_reason") == "level_filtered" and r.get("send_level") == 5 for r in decisions):
        raise SystemExit(f"x11/{mode}: missing Hotstring equal-level filter")
    trigger = next((r for r in decisions if r.get("consumer") == "hotstring"
                    and r.get("consumer_action") == "triggered"
                    and r.get("consumer_reason") == "hotstring_matched"
                    and r.get("send_level") == 6), None)
    if not trigger:
        raise SystemExit(f"x11/{mode}: missing Hotstring trigger")
    trigger_i = rr.index(trigger)
    trigger_out_i = next((i for i, r in enumerate(rr) if i > trigger_i
                          and r.get("consumer") == "hotstring"
                          and r.get("stage") == "consumer_outcome"
                          and r.get("outcome") == "hotstring_dispatched"), -1)
    if trigger_out_i <= trigger_i:
        raise SystemExit(f"x11/{mode}: Hotstring decision/outcome order wrong")
    if not any(r.get("domain") == "x11_raw" and r.get("consumer") == "inputhook"
               and r.get("consumer_reason") == "selected_grab_owns_event" for r in decisions):
        raise SystemExit(f"x11/{mode}: raw selected-grab ownership trace missing")
    if not any(r.get("domain") == "x11_grab" and r.get("consumer") == "inputhook"
               and r.get("consumer_reason") == "end_key" for r in decisions):
        raise SystemExit(f"x11/{mode}: grab EndKey decision missing")
    report[f"x11_{mode}_consumer_decisions"] = len(decisions)

# Broker physical InputHook reuses the two hotkey-adapter acceptances. It must
# not produce a second capture/reduce pair in the capture consumer.
bi = rows("broker-ih.trace")
f, text = fields("broker-ih.out")
if any(f.get(k) != v for k, v in {"kd":"1", "ku":"1", "chars":"a", "source":"inputd"}.items()):
    raise SystemExit(f"broker InputHook result mismatch: {text}")
if len([r for r in bi if r.get("stage") == "capture"]) != 2 or len([r for r in bi if r.get("stage") == "reduce"]) != 2:
    raise SystemExit("broker InputHook duplicated acceptance/reducer")
if not any(r.get("consumer") == "inputhook" and r.get("outcome") == "callback_dispatched" for r in bi):
    raise SystemExit("broker InputHook callback outcome missing")
if not all(r.get("domain") == "inputd" for r in bi):
    raise SystemExit("broker InputHook domain mismatch")

bh = rows("broker-hs.trace")
f, text = fields("broker-hs.out")
if any(f.get(k) != v for k, v in {"hs":"1", "level":"0", "source":"inputd"}.items()):
    raise SystemExit(f"broker Hotstring result mismatch: {text}")
if len([r for r in bh if r.get("stage") == "capture"]) != 6 or len([r for r in bh if r.get("stage") == "reduce"]) != 6:
    raise SystemExit("broker Hotstring duplicated acceptance/reducer")
btrigger = next((r for r in bh if r.get("consumer") == "hotstring"
                 and r.get("consumer_action") == "triggered"
                 and r.get("consumer_reason") == "hotstring_matched"), None)
if not btrigger:
    raise SystemExit("broker Hotstring trigger decision missing")
bi0 = bh.index(btrigger)
bo0 = next((i for i, r in enumerate(bh) if i > bi0
            and r.get("consumer") == "hotstring"
            and r.get("stage") == "consumer_outcome"
            and r.get("outcome") == "hotstring_dispatched"), -1)
if bo0 <= bi0:
    raise SystemExit("broker Hotstring decision/outcome order wrong")
if not all(r.get("domain") == "inputd" for r in bh):
    raise SystemExit("broker Hotstring domain mismatch")

report.update({"broker_inputhook_acceptances": 2, "broker_hotstring_acceptances": 6,
               "broker_inputhook_callbacks": True, "broker_hotstring_triggered": True})
summary.write_text(json.dumps({"schema":1,"result":"pass",**report}, sort_keys=True)+"\n", encoding="utf-8")
print(summary.read_text(encoding="utf-8").strip())
