#!/bin/bash
# M5b-3 broker remap -> normalized child transaction pipeline oracle.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
AHK="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BROKER" in /*) ;; *) BROKER="$ROOT/$BROKER" ;; esac
case "$AHK" in /*) ;; *) AHK="$ROOT/$AHK" ;; esac
OUT="$ROOT/tests/oracle/out"; mkdir -p "$OUT"
WORK=/tmp/input-remap-pipeline; rm -rf "$WORK"; mkdir -p "$WORK"
PROBE="$WORK/inputd-v2-probe"; FIXTURE="$WORK/inputd-test-fixture"; WATCH="$WORK/inputd-output-watch"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_v2_probe.c" -o "$PROBE"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_output_watch.c" -o "$WATCH"
sudo -n true 2>/dev/null || { echo "remap pipeline oracle needs sudo -n"; exit 1; }
cleanup() {
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
  sudo -n pkill -9 -f "^$FIXTURE " 2>/dev/null || true
  sudo -n pkill -9 -f "^$PROBE " 2>/dev/null || true
  sudo -n pkill -9 -f "^$WATCH " 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
cleanup
TRIG="$WORK/trigger"; DEV="$WORK/dev"; rm -f "$TRIG" "$DEV"
sudo -n env AHK_FIXTURE_NAME="remap-pipeline-$$" AHK_FIXTURE_DEVPATH="$DEV" \
  "$FIXTURE" --seq-trigger 1 30 "$TRIG" >"$WORK/fixture.log" 2>&1 &
for _ in $(seq 1 100); do [ -s "$DEV" ] && break; sleep .03; done
node=$(cat "$DEV"); for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .03; done
sudo -n env AHK_INPUTD_TEST_DEVICE="$node" "$BROKER" --socket "$WORK/sock" --socket-mode 0666 -v >"$WORK/broker.log" 2>&1 &
for _ in $(seq 1 120); do grep -q grabbed "$WORK/broker.log" 2>/dev/null && break; sleep .03; done
sudo -n "$PROBE" "$WORK/sock" arb 30 3 800 10 8000 48 7 0 --stay 6000 >"$WORK/owner.log" 2>&1 &
for _ in $(seq 1 100); do grep -q 'ARB_ACK reg=800 status=0' "$WORK/owner.log" 2>/dev/null && break; sleep .03; done
grep -q 'ARB_ACK reg=800 status=0' "$WORK/owner.log"
cat >"$WORK/remap.ahk" <<'EOF'
#Requires AutoHotkey v2.0
#InputLevel 5
count := 0
OnB(*) {
    global count
    count += 1
    h := HotkeyBackendGet("~b")
    FileAppend("count=" count " level=" A_SendLevel " this=" A_ThisHotkey " source=" h.state_source "`n", A_Args[1])
    SetTimer(Finish, -250)
}
Finish() {
    global count
    ExitApp(count = 1 ? 0 : 8)
}
Hotkey("~b", OnB)
FileAppend("ready`n", A_Args[2])
SetTimer(() => ExitApp(9), -6000)
EOF
AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$WORK/sock" AHK_INPUT_PIPELINE=active \
  AHK_INPUT_PIPELINE_TRACE="$WORK/trace" xvfb-run -a "$AHK" "$WORK/remap.ahk" \
  "$WORK/out" "$WORK/ready" >"$WORK/ahk.log" 2>&1 & AP=$!
for _ in $(seq 1 200); do [ -f "$WORK/ready" ] && grep -q 'subscribed' "$WORK/broker.log" 2>/dev/null && break; sleep .03; done
sudo -n "$WATCH" --count 2 --timeout-ms 5000 >"$WORK/target.log" 2>&1 &
for _ in $(seq 1 100); do grep -q OUTPUT_DEVICE "$WORK/target.log" 2>/dev/null && break; sleep .03; done
touch "$TRIG"; wait "$AP"
for _ in $(seq 1 100); do grep -q 'OUTPUT_END count=2' "$WORK/target.log" 2>/dev/null && break; sleep .03; done
grep -q 'OUT code=48 value=1' "$WORK/target.log"
grep -q 'OUT code=48 value=0' "$WORK/target.log"
! grep -q 'OUT code=30' "$WORK/target.log"
grep -q '^count=1 level=5 this=~b source=inputd$' "$WORK/out"
python3 - "$WORK/trace" "$OUT/input-remap-pipeline-summary.json" <<'PY'
import json, pathlib, sys
rows=[json.loads(x) for x in pathlib.Path(sys.argv[1]).read_text().splitlines() if x]
child=next((r for r in rows if r.get('stage')=='capture' and r.get('evdev_code')==48 and not r.get('release')),None)
if not child or child.get('source')!='other_inject' or child.get('send_level')!=7:
    raise SystemExit(f'bad remap child: {child}')
broker=next((r for r in rows if r.get('stage')=='broker_decision' and r.get('broker_action')=='remap'
             and r.get('replacement_transaction_id')==child.get('transaction_id')),None)
if not broker:
    raise SystemExit(f'linked broker decision missing: child={child}, rows={rows}')
if broker.get('source_transaction_id') != child.get('parent_transaction_id') or not child.get('parent_transaction_id'):
    raise SystemExit(f'parent transaction mismatch: {broker} {child}')
match=next((r for r in rows if r.get('stage')=='match' and r.get('evdev_code')==48
            and r.get('action')=='trigger_pass'),None)
if not match:
    raise SystemExit('remap child did not trigger InputLevel-5 hotkey')
summary={'schema':1,'result':'pass','source_transaction':broker['source_transaction_id'],
         'replacement_transaction':child['transaction_id'],'send_level':7,
         'input_level':5,'target_sequence':['48:1','48:0'],'original_suppressed':True}
pathlib.Path(sys.argv[2]).write_text(json.dumps(summary,sort_keys=True)+'\n')
print(json.dumps(summary,sort_keys=True))
PY
echo "INPUT_REMAP_PIPELINE_ORACLE_PASS"
