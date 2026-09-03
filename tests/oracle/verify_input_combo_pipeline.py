#!/usr/bin/env python3
from __future__ import annotations
import json, pathlib, sys
if len(sys.argv) != 3:
    raise SystemExit("usage: verify_input_combo_pipeline.py WORK SUMMARY")
work=pathlib.Path(sys.argv[1]); summary=pathlib.Path(sys.argv[2])
# P2-11 matrix: bare a&b fires twice (once bare, once while an unrelated
# Ctrl is held — custom combos ignore extra modifiers, matching the Windows
# golden behavior), a&c-up once, tilde a&d once, solo-e once, sc e&f once,
# and the shared-prefix e&g once. Wrong-second-key and prefix-repeat
# segments must fire nothing.
expected={"down":2,"up":1,"tilde":1,"solo-e":1,"e-f":1,"e-g":1}
report={}
for mode in ("active","mirror","legacy"):
    lines=(work/f"{mode}.out").read_text(encoding="utf-8").splitlines()
    got={k:sum(x.startswith(k+" ") for x in lines) for k in expected}
    if got != expected:
        raise SystemExit(f"{mode}: callbacks {got}, want {expected}: {lines}")
    if any("level=3" not in x for x in lines if not x.startswith("final ")):
        raise SystemExit(f"{mode}: combo callback SendLevel mismatch: {lines}")
    if f"final mode={mode} total=7" not in lines:
        raise SystemExit(f"{mode}: final mismatch: {lines}")
    rows=[json.loads(x) for x in (work/f"{mode}.trace").read_text(encoding="utf-8").splitlines() if x]
    combos=[r for r in rows if r.get("reason") in ("combo_prefix_held","combo_suffix","combo_release","combo_standalone","keyup_ownership")]
    if not combos:
        raise SystemExit(f"{mode}: no combo decisions")
    if mode == "active":
        # Suffix firings grouped per registration: bare a&b (1) fires twice —
        # once bare, once while Ctrl is held; e&g (6) fires once.
        suffix=[r.get("registration_id") for r in combos if r.get("stage")=="decision" and r.get("reason")=="combo_suffix" and r.get("action","").startswith("trigger_")]
        if suffix != [1,2,3,4,6,1]:
            raise SystemExit(f"active: combo suffix sequence {suffix}")
        if not any(r.get("reason")=="combo_standalone" and r.get("registration_id")==5 for r in combos):
            raise SystemExit("active: standalone prefix decision missing")
        if not any(r.get("reason")=="keyup_ownership" and r.get("registration_id")==2 for r in combos):
            raise SystemExit("active: combo key-up ownership missing")
        if not any(r.get("reason")=="combo_suffix" and r.get("registration_id")==3 and r.get("action")=="trigger_pass" for r in combos):
            raise SystemExit("active: tilde combo pass decision missing")
    else:
        mirrors=[r for r in rows if r.get("stage")=="combo_mirror"]
        if not mirrors or not all(r.get("equivalent") is True for r in mirrors):
            raise SystemExit(f"{mode}: combo mirror mismatch {mirrors}")
    report[f"{mode}_combo_decisions"]=len(combos)
summary.write_text(json.dumps({"schema":1,"result":"pass",**report},sort_keys=True)+"\n",encoding="utf-8")
print(summary.read_text(encoding="utf-8").strip())
