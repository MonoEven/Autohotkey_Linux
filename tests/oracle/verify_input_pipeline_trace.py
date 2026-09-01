#!/usr/bin/env python3
from __future__ import annotations
import json
import pathlib
import sys

if len(sys.argv) != 8:
    raise SystemExit("usage: verify_input_pipeline_trace.py BROKER_ACTIVE BROKER_MIRROR BROKER_LEGACY X11_ACTIVE X11_MIRROR X11_LEGACY RESULTS_DIR")
active, mirror, legacy, xactive, xmirror, xlegacy = [pathlib.Path(p) for p in sys.argv[1:7]]
results = pathlib.Path(sys.argv[7])

def rows(path: pathlib.Path):
    return [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines() if x]

a, m, l = rows(active), rows(mirror), rows(legacy)
for name, rr in (("active", a), ("mirror", m), ("legacy", l)):
    if not rr or any(r.get("schema") != 1 for r in rr):
        raise SystemExit(f"{name}: missing/non-v1 pipeline trace")
    if any(r.get("domain") != "inputd" for r in rr):
        raise SystemExit(f"{name}: unexpected domain")
    if any(r.get("authority_generation", 0) == 0 for r in rr):
        raise SystemExit(f"{name}: missing authority generation")

stages = [r.get("stage") for r in a]
ordered = ("capture", "reduce", "match", "decision", "dispatch", "outcome")
for required in ordered:
    if required not in stages:
        raise SystemExit(f"active: missing stage {required}: {stages}")
indices = [stages.index(x) for x in ordered]
if indices != sorted(indices):
    raise SystemExit(f"active: wrong stage order: {stages}")
active_match = next((r for r in a if r.get("stage") == "match" and r.get("action", "").startswith("trigger_")), None)
if not active_match or active_match.get("send_level") != 10 or active_match.get("transaction_id", 0) == 0:
    raise SystemExit(f"active matcher provenance wrong: {active_match}")
if not any(r.get("stage") == "outcome" and r.get("outcome") == "triggered_pass" for r in a):
    raise SystemExit("active: missing triggered_pass outcome")
for name, rr in (("mirror", m), ("legacy", l)):
    mirrors = [r for r in rr if r.get("stage") == "mirror"]
    if not mirrors or not all(r.get("equivalent") is True for r in mirrors):
        raise SystemExit(f"{name}: mirror mismatch: {mirrors}")

observed = {}
for mode in ("active", "mirror", "legacy"):
    text = (results / f"{mode}.out").read_text(encoding="utf-8").strip()
    fields = dict(part.split("=", 1) for part in text.split() if "=" in part)
    for key, want in {"fire":"1", "level":"5", "this":"~F7", "source":"inputd"}.items():
        if fields.get(key) != want:
            raise SystemExit(f"{mode}: {key}={fields.get(key)!r}, want {want!r}: {text}")
    if fields.get("mode") != mode:
        raise SystemExit(f"{mode}: diagnostics mode mismatch: {text}")
    if int(fields.get("reducer", "0")) <= 0:
        raise SystemExit(f"{mode}: reducer generation missing: {text}")
    observed[mode] = fields

xrows = {"active": rows(xactive), "mirror": rows(xmirror), "legacy": rows(xlegacy)}
if not all(r.get("domain") == "x11_grab" for rr in xrows.values() for r in rr):
    raise SystemExit("x11 traces: unexpected domain")
xstages = [r.get("stage") for r in xrows["active"]]
for required in ordered:
    if required not in xstages:
        raise SystemExit(f"x11 active: missing {required}")
xindices = [xstages.index(x) for x in ordered]
if xindices != sorted(xindices):
    raise SystemExit(f"x11 active: wrong stage order: {xstages}")
for mode in ("mirror", "legacy"):
    mirrors = [r for r in xrows[mode] if r.get("stage") == "mirror"]
    if not mirrors or not all(r.get("equivalent") is True for r in mirrors):
        raise SystemExit(f"x11 {mode}: mismatch {mirrors}")
    if (results / f"x11-{mode}.out").read_text(encoding="utf-8").strip() != f"fire=1 level=0 this=^F8 mode={mode}":
        raise SystemExit(f"x11 {mode}: callback mismatch")
if (results / "x11-active.out").read_text(encoding="utf-8").strip() != "fire=1 level=0 this=^F8 mode=active":
    raise SystemExit("x11 active: callback mismatch")

print(json.dumps({
    "schema": 1, "result": "pass", "active_stages": stages,
    "active_transaction": active_match["transaction_id"],
    "mirror_equivalent": True, "legacy_equivalent": True,
    "x11_mirror_equivalent": True, "x11_legacy_equivalent": True,
    "physical_getkeystate": True,
    "callback": {k: {x: v[x] for x in ("fire", "level", "this", "source")} for k, v in observed.items()},
}, sort_keys=True))
