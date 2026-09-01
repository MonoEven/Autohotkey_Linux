#!/bin/bash
# M5b-1 normalized wildcard/LR/key-up hotkey oracle.
# Runs identical sequences through inputd and X11 in active/mirror/legacy.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
AHK="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BROKER" in /*) ;; *) BROKER="$ROOT/$BROKER" ;; esac
case "$AHK" in /*) ;; *) AHK="$ROOT/$AHK" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
WORK=/tmp/input-pipeline-special
rm -rf "$WORK"; mkdir -p "$WORK"
FIXTURE="$WORK/inputd-test-fixture"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE"
sudo -n true 2>/dev/null || { echo "special pipeline oracle needs sudo -n"; exit 1; }
cleanup() {
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
  sudo -n pkill -9 -f "^$FIXTURE " 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
cleanup
cat >"$WORK/special.ahk" <<'EOF'
#Requires AutoHotkey v2.0
#InputLevel 4
lr := 0, exact := 0, wild := 0, up := 0
Log(name, count) {
    FileAppend(name "=" count " level=" A_SendLevel " this=" A_ThisHotkey "`n", A_Args[1])
    CheckDone()
}
CheckDone() {
    global lr, exact, wild, up
    if (lr = 1 && exact = 1 && wild = 1 && up = 1)
        SetTimer(Finish, -150)
}
Finish() {
    global lr, exact, wild, up
    h := HotkeyBackendGet()
    FileAppend("final lr=" lr " exact=" exact " wild=" wild " up=" up " mode=" h.pipeline_mode "`n", A_Args[1])
    ExitApp(lr = 1 && exact = 1 && wild = 1 && up = 1 ? 0 : 7)
}
OnLR(*) {
    global lr
    lr += 1
    Log("lr", lr)
}
OnExact(*) {
    global exact
    exact += 1
    Log("exact", exact)
}
OnWild(*) {
    global wild
    wild += 1
    Log("wild", wild)
}
OnUp(*) {
    global up
    up += 1
    Log("up", up)
}
Hotkey("<^F7", OnLR)
Hotkey("+F8", OnExact)
Hotkey("*F8", OnWild)
Hotkey("F9 up", OnUp)
FileAppend("ready`n", A_Args[2])
SetTimer(Finish, -7000)
EOF

# KEY_RIGHTCTRL=97, LEFTCTRL=29, LEFTSHIFT=42, LEFTALT=56,
# F7=65, F8=66, F9=67. Wrong side first, then correct LR, exact-vs-wildcard,
# wildcard with extra Alt, then key-up.
sequence=(97:1 65:1 65:0 97:0 29:1 65:1 65:0 29:0 \
          42:1 66:1 66:0 42:0 56:1 66:1 66:0 56:0 67:1 67:0)

for mode in active mirror legacy; do
  cleanup; sleep .2
  trigger="$WORK/broker-$mode.trigger"; devfile="$WORK/broker-$mode.dev"
  rm -f "$trigger" "$devfile" "$WORK/broker-$mode.out" "$WORK/broker-$mode.ready" "$WORK/broker-$mode.trace"
  sudo -n env AHK_FIXTURE_NAME="special-$mode-$$" AHK_FIXTURE_DEVPATH="$devfile" \
    "$FIXTURE" --script-trigger "$trigger" "${sequence[@]}" >"$WORK/broker-$mode-fixture.log" 2>&1 &
  for _ in $(seq 1 100); do [ -s "$devfile" ] && break; sleep .03; done
  node=$(cat "$devfile"); for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .03; done
  sock="$WORK/broker-$mode.sock"; sudo -n rm -f "$sock" "$sock.lock"
  sudo -n env AHK_INPUTD_TEST_DEVICE="$node" "$BROKER" --socket "$sock" --socket-mode 0666 -v >"$WORK/broker-$mode-broker.log" 2>&1 &
  for _ in $(seq 1 120); do grep -q grabbed "$WORK/broker-$mode-broker.log" 2>/dev/null && break; sleep .03; done
  ( cd "$WORK" && sudo -n env AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$sock" \
      AHK_INPUT_PIPELINE="$mode" AHK_INPUT_PIPELINE_TRACE="$WORK/broker-$mode.trace" \
      xvfb-run -a "$AHK" special.ahk "$WORK/broker-$mode.out" "$WORK/broker-$mode.ready" \
      >"$WORK/broker-$mode-ahk.log" 2>&1 ) & AP=$!
  for _ in $(seq 1 250); do
    [ -f "$WORK/broker-$mode.ready" ] && grep -q 'subscribed' "$WORK/broker-$mode-broker.log" 2>/dev/null && break
    sleep .03
  done
  grep -q 'subscribed' "$WORK/broker-$mode-broker.log"
  touch "$trigger"; wait "$AP"
  grep -q '^final lr=1 exact=1 wild=1 up=1 ' "$WORK/broker-$mode.out"
done

for mode in active mirror legacy; do
  rm -f "$WORK/x11-$mode.out" "$WORK/x11-$mode.ready" "$WORK/x11-$mode.trace"
  xvfb-run -a bash -c '
    mode=$1; ahk=$2; work=$3
    AHK_INPUT_PIPELINE="$mode" AHK_INPUT_PIPELINE_TRACE="$work/x11-$mode.trace" \
      "$ahk" "$work/special.ahk" "$work/x11-$mode.out" "$work/x11-$mode.ready" \
      >"$work/x11-$mode-ahk.log" 2>&1 & pid=$!
    for i in $(seq 1 200); do [ -f "$work/x11-$mode.ready" ] && break; sleep .03; done
    [ -f "$work/x11-$mode.ready" ]
    xdotool keyup Control_L; xdotool keyup Control_R; xdotool keyup Shift_L; xdotool keyup Shift_R
    xdotool keyup Alt_L; xdotool keyup Alt_R; xdotool keyup F7; xdotool keyup F8; xdotool keyup F9; sleep .25
    xdotool keydown Control_R; sleep .08; xdotool keydown F7; sleep .08; xdotool keyup F7; sleep .08; xdotool keyup Control_R; sleep 1.2
    xdotool keydown Control_L; sleep .08; xdotool keydown F7; sleep .08; xdotool keyup F7; sleep .08; xdotool keyup Control_L; sleep .3
    xdotool keydown Shift_L; sleep .08; xdotool keydown F8; sleep .08; xdotool keyup F8; sleep .08; xdotool keyup Shift_L; sleep .3
    xdotool keydown Alt_L; sleep .08; xdotool keydown F8; sleep .08; xdotool keyup F8; sleep .08; xdotool keyup Alt_L; sleep .3
    xdotool keydown F9; sleep .1; xdotool keyup F9
    wait "$pid"
  ' _ "$mode" "$AHK" "$WORK"
  grep -q '^final lr=1 exact=1 wild=1 up=1 ' "$WORK/x11-$mode.out"
done

python3 "$ROOT/tests/oracle/verify_input_pipeline_special_trace.py" "$WORK" \
  "$OUT/input-pipeline-special-summary.json"
echo "INPUT_PIPELINE_SPECIAL_ORACLE_PASS"
