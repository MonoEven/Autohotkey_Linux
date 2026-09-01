#!/usr/bin/env python3
from __future__ import annotations
import json
import pathlib
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: verify_input_pipeline_special_trace.py WORK SUMMARY")
work = pathlib.Path(sys.argv[1])
summary = pathlib.Path(sys.argv[2])

expected_lines = {
    "lr=1 level=4 this=<^F7",
    "exact=1 level=4 this=+F8",
    "wild=1 level=4 this=*F8",
    "up=1 level=4 this=F9 up",
}

def read_rows(path: pathlib.Path):
    return [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines() if x]

report = {}
for lane, domain in (("broker", "inputd"), ("x11", "x11_grab")):
    for mode in ("active", "mirror", "legacy"):
        out = (work / f"{lane}-{mode}.out").read_text(encoding="utf-8").splitlines()
        actual = {x for x in out if not x.startswith("final ")}
        if actual != expected_lines:
            raise SystemExit(f"{lane}/{mode}: callback mismatch: {out}")
        final = next((x for x in out if x.startswith("final ")), "")
        want_final = f"final lr=1 exact=1 wild=1 up=1 mode={mode}"
        if final != want_final:
            raise SystemExit(f"{lane}/{mode}: final={final!r}, want {want_final!r}")
        rows = read_rows(work / f"{lane}-{mode}.trace")
        if not rows or any(r.get("domain") != domain for r in rows):
            raise SystemExit(f"{lane}/{mode}: bad/missing trace")
        mirrors = [r for r in rows if r.get("stage") == "mirror"]
        if mode != "active" and (not mirrors or not all(r.get("equivalent") is True for r in mirrors)):
            raise SystemExit(f"{lane}/{mode}: mirror mismatch {mirrors}")
        report[f"{lane}_{mode}_events"] = len({r.get("acceptance_seq") for r in rows})

    active = read_rows(work / f"{lane}-active.trace")
    matches = [r for r in active if r.get("stage") == "match"]
    f7 = [r for r in matches if r.get("evdev_code") == 65 and not r.get("release")]
    if len(f7) < 2 or f7[0].get("action") != "no_match" or not f7[1].get("action", "").startswith("trigger_"):
        raise SystemExit(f"{lane}: LR wrong/correct-side sequence mismatch: {f7}")
    f8_triggers = [r for r in matches if r.get("evdev_code") == 66 and r.get("action", "").startswith("trigger_")]
    regs = [r.get("registration_id") for r in f8_triggers]
    if regs != [2, 3]:
        raise SystemExit(f"{lane}: exact/wildcard resolution expected [2,3], got {regs}: {f8_triggers}")
    keyup_hold = [r for r in matches if r.get("evdev_code") == 67 and not r.get("release")
                  and r.get("reason") == "keyup_ownership"]
    keyup_fire = [r for r in matches if r.get("evdev_code") == 67 and r.get("release")
                  and r.get("action", "").startswith("trigger_")]
    if not keyup_hold or not keyup_fire:
        raise SystemExit(f"{lane}: key-up ownership/fire missing")
    if lane == "broker" and keyup_hold[0].get("action") != "suppress_original":
        raise SystemExit(f"broker: key-up down was not suppressed: {keyup_hold[0]}")
    report[f"{lane}_exact_wildcard"] = regs
    report[f"{lane}_keyup_owned"] = True

summary.write_text(json.dumps({"schema":1,"result":"pass",**report}, sort_keys=True) + "\n", encoding="utf-8")
print(summary.read_text(encoding="utf-8").strip())
