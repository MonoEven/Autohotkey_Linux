#!/bin/bash
# Normalized input pipeline M5a oracle (check0901 P1-1 / detail §4).
# Runs the same ordinary broker hotkey in active/mirror/legacy modes and proves
# identical callback state plus full active trace and mirror equivalence.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
AHK="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BROKER" in /*) ;; *) BROKER="$ROOT/$BROKER" ;; esac
case "$AHK" in /*) ;; *) AHK="$ROOT/$AHK" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
WORK=/tmp/input-pipeline
rm -rf "$WORK"; mkdir -p "$WORK"
PROBE="$WORK/inputd-v2-probe"
FIXTURE="$WORK/inputd-test-fixture"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_v2_probe.c" -o "$PROBE"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE"
sudo -n true 2>/dev/null || { echo "input pipeline oracle needs sudo -n"; exit 1; }
for p in $(pgrep -x ahk-inputd); do sudo -n kill -9 "$p" 2>/dev/null || true; done
sudo -n pkill -9 -x inputd-test-fixture 2>/dev/null || true
FIXTURE_PID=""
cleanup() {
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
  sudo -n pkill -9 -x inputd-test-fixture 2>/dev/null || true
  [ -n "$FIXTURE_PID" ] && sudo -n kill -9 "$FIXTURE_PID" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
cat >"$WORK/input_pipeline_test.ahk" <<'EOF'
#Requires AutoHotkey v2.0
#InputLevel 5
fired := 0
OnF7(*) {
    global fired
    fired += 1
    h := HotkeyBackendGet("~F7")
    FileAppend("fire=" fired " level=" A_SendLevel " this=" A_ThisHotkey " source=" h.state_source " mode=" h.pipeline_mode " reducer=" h.reducer_generation "`n", A_Args[1])
    SetTimer(() => ExitApp(0), -50)
}
Hotkey("~F7", OnF7)
SetTimer(() => ExitApp(8), -7000)
EOF

for mode in active mirror legacy; do
  SOCK="$WORK/$mode.sock"
  sudo -n rm -f "$SOCK" "$SOCK.lock"
  sudo -n env AHK_INPUTD_TEST_DEVICE=name:definitely-not-a-device "$BROKER" \
    --socket "$SOCK" --socket-mode 0666 -v >"$WORK/$mode-broker.log" 2>&1 &
  BP=$!
  for _ in $(seq 1 100); do [ -S "$SOCK" ] && break; sleep .03; done
  rm -f "$WORK/$mode.out" "$WORK/$mode.trace" "$WORK/$mode-ahk.log"
  ( cd "$WORK" && AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$SOCK" \
      AHK_INPUT_PIPELINE="$mode" AHK_INPUT_PIPELINE_TRACE="$WORK/$mode.trace" \
      xvfb-run -a "$AHK" input_pipeline_test.ahk "$WORK/$mode.out" >"$WORK/$mode-ahk.log" 2>&1 ) &
  AP=$!
  for _ in $(seq 1 200); do grep -q 'broker mode active' "$WORK/$mode-ahk.log" 2>/dev/null && break; sleep .03; done
  grep -q 'broker mode active' "$WORK/$mode-ahk.log"
  sleep .25
  sudo -n "$PROBE" "$SOCK" inject 65 10 --txn "$((900 + ${#mode}))" >/dev/null
  wait "$AP"
  grep -q '^fire=1 ' "$WORK/$mode.out"
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
  sleep .2
done

# Physical reducer/GetKeyState source (active pipeline).
cat >"$WORK/input_pipeline_physical.ahk" <<'EOF'
#Requires AutoHotkey v2.0
OnF7(*) {
    h := HotkeyBackendGet("~F7")
    FileAppend("physical=" GetKeyState("F7", "P") " source=" h.state_source " reducer=" h.reducer_generation "`n", A_Args[1])
    SetTimer(() => ExitApp(0), -50)
}
Hotkey("~F7", OnF7)
FileAppend("ready`n", A_Args[2])
SetTimer(() => ExitApp(10), -7000)
EOF
PTRIG="$WORK/physical.trigger"; PDEV="$WORK/physical.dev"
rm -f "$PTRIG" "$PDEV" "$WORK/physical.out" "$WORK/physical.ready" "$WORK/physical.trace"
sudo -n env AHK_FIXTURE_NAME="pipeline-physical-$$" AHK_FIXTURE_DEVPATH="$PDEV" \
  "$FIXTURE" --seq-trigger 1 65 "$PTRIG" >"$WORK/physical-fixture.log" 2>&1 & FP=$!
FIXTURE_PID=$FP
for _ in $(seq 1 100); do [ -s "$PDEV" ] && break; sleep .03; done
node=$(cat "$PDEV")
for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .03; done
sudo -n rm -f "$WORK/physical.sock" "$WORK/physical.sock.lock"
sudo -n env AHK_INPUTD_TEST_DEVICE="$node" "$BROKER" --socket "$WORK/physical.sock" \
  --socket-mode 0666 -v >"$WORK/physical-broker.log" 2>&1 & BP=$!
for _ in $(seq 1 120); do grep -q grabbed "$WORK/physical-broker.log" 2>/dev/null && break; sleep .03; done
( cd "$WORK" && AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$WORK/physical.sock" \
    AHK_INPUT_PIPELINE=active AHK_INPUT_PIPELINE_TRACE="$WORK/physical.trace" \
    xvfb-run -a "$AHK" input_pipeline_physical.ahk "$WORK/physical.out" "$WORK/physical.ready" \
    >"$WORK/physical-ahk.log" 2>&1 ) & AP=$!
for _ in $(seq 1 200); do
  [ -f "$WORK/physical.ready" ] && grep -q 'broker mode active' "$WORK/physical-ahk.log" 2>/dev/null && break
  sleep .03
done
grep -q 'broker mode active' "$WORK/physical-ahk.log"
for _ in $(seq 1 100); do
  hs=$("$PROBE" "$WORK/physical.sock" health 2>/dev/null || true)
  echo "$hs" | grep -Eq 'registrations=[1-9][0-9]*' && break
  sleep .05
done
echo "$hs" | grep -Eq 'registrations=[1-9][0-9]*'
"$PROBE" "$WORK/physical.sock" watch 65:0 --until-events 2 --timeout-ms 6000 >"$WORK/physical-observer.log" 2>&1 & OP=$!
for _ in $(seq 1 100); do grep -q '^SUBSCRIBE_ACK ' "$WORK/physical-observer.log" 2>/dev/null && break; sleep .03; done
grep -q '^SUBSCRIBE_ACK ' "$WORK/physical-observer.log"
touch "$PTRIG"; wait "$AP"; wait "$OP"
grep -q '^physical=1 source=inputd reducer=' "$WORK/physical.out"
grep -q '"source":"physical"' "$WORK/physical.trace"
grep -q 'src=0 conf=0 level=-1' "$WORK/physical-observer.log"
sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
sudo -n pkill -9 -x inputd-test-fixture 2>/dev/null || true
sudo -n kill -9 "$BP" "$FP" 2>/dev/null || true
FIXTURE_PID=""
sleep .2

cat >"$WORK/input_pipeline_x11.ahk" <<'EOF'
#Requires AutoHotkey v2.0
OnF8(*) {
    h := HotkeyBackendGet("F8")
    FileAppend("fire=1 level=" A_SendLevel " this=" A_ThisHotkey " mode=" h.pipeline_mode "`n", A_Args[1])
    SetTimer(() => ExitApp(0), -50)
}
Hotkey("^F8", OnF8)
FileAppend("ready`n", A_Args[2])
SetTimer(() => ExitApp(9), -7000)
EOF
for mode in active mirror legacy; do
  rm -f "$WORK/x11-$mode.out" "$WORK/x11-$mode.ready" "$WORK/x11-$mode.trace" "$WORK/x11-$mode.log"
  xvfb-run -a bash -c '
    mode=$1; ahk=$2; work=$3
    AHK_INPUT_PIPELINE="$mode" AHK_INPUT_PIPELINE_TRACE="$work/x11-$mode.trace" \
      "$ahk" "$work/input_pipeline_x11.ahk" "$work/x11-$mode.out" "$work/x11-$mode.ready" \
      >"$work/x11-$mode.log" 2>&1 & pid=$!
    for i in $(seq 1 200); do [ -f "$work/x11-$mode.ready" ] && break; sleep .03; done
    [ -f "$work/x11-$mode.ready" ]
    xdotool keydown ctrl; sleep .05; xdotool keydown F8; sleep .08; xdotool keyup F8; xdotool keyup ctrl
    wait "$pid"
  ' _ "$mode" "$AHK" "$WORK"
  grep -q '^fire=1 ' "$WORK/x11-$mode.out"
done

python3 "$ROOT/tests/oracle/verify_input_pipeline_trace.py" \
  "$WORK/active.trace" "$WORK/mirror.trace" "$WORK/legacy.trace" \
  "$WORK/x11-active.trace" "$WORK/x11-mirror.trace" "$WORK/x11-legacy.trace" \
  "$WORK" | tee "$OUT/input-pipeline-summary.json"
echo "INPUT_PIPELINE_ORACLE_PASS"
