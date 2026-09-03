#!/bin/bash
# M5b-3 custom-combo normalized pipeline equivalence oracle.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
AHK="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BROKER" in /*) ;; *) BROKER="$ROOT/$BROKER" ;; esac
case "$AHK" in /*) ;; *) AHK="$ROOT/$AHK" ;; esac
OUT="$ROOT/tests/oracle/out"; mkdir -p "$OUT"
WORK=/tmp/input-combo-pipeline; rm -rf "$WORK"; mkdir -p "$WORK"
FIXTURE="$WORK/inputd-test-fixture"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE"
sudo -n true 2>/dev/null || { echo "combo pipeline oracle needs sudo -n"; exit 1; }
cleanup() {
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
  sudo -n pkill -9 -f "^$FIXTURE " 2>/dev/null || true
  return 0
}
trap cleanup EXIT HUP INT TERM
cleanup || true
cat >"$WORK/combo.ahk" <<'EOF'
#Requires AutoHotkey v2.0
#InputLevel 3
total := 0
Log(name) {
    global total
    total += 1
    FileAppend(name " level=" A_SendLevel " this=" A_ThisHotkey "`n", A_Args[1])
}
OnDown(*) {
    Log("down")
}
OnUp(*) {
    Log("up")
}
OnTilde(*) {
    Log("tilde")
}
OnSoloE(*) {
    Log("solo-e")
}
OnEF(*) {
    Log("e-f")
}
OnEG(*) {
    Log("e-g")
}
Finish() {
    global total
    h := HotkeyBackendGet("a & b")
    FileAppend("final mode=" h.pipeline_mode " total=" total "`n", A_Args[1])
    ExitApp(total = 7 ? 0 : 7)
}
Hotkey("a & b", OnDown)
Hotkey("a & c up", OnUp)
Hotkey("~a & d", OnTilde)
Hotkey("sc012 & sc021", OnEF)
Hotkey("e", OnSoloE)
; P2-11 matrix additions: shared prefix (two combos on the same prefix key
; must each fire exactly once for their own suffix), extra-modifier tolerance
; (a&b must still fire while an unrelated Ctrl is held — custom combos ignore
; extra modifiers, matching the Windows golden behavior; modifier symbols on
; the PREFIX itself such as "+a & b" are rejected exactly like upstream), a
; prefix auto-repeat burst (repeat events must never fire the combo or the
; standalone prefix while the prefix is held), and a wrong-second-key segment
; (a held, f pressed) that must fire nothing because the e&f prefix is not
; held.
Hotkey("e & g", OnEG)
FileAppend("ready`n", A_Args[2])
SetTimer(Finish, -5000)
EOF
sequence=(29:1 30:1 48:1 48:0 30:0 29:0 \
          30:1 46:1 46:0 30:0 30:1 32:1 32:0 30:0 \
          18:1 18:0 18:1 33:1 33:0 18:0 \
          30:1 30:2 30:2 30:0 \
          30:1 33:1 33:0 30:0 \
          18:1 34:1 34:0 18:0 \
          29:1 42:1 30:1 48:1 48:0 30:0 42:0 29:0)
for mode in active mirror legacy; do
  cleanup; sleep .25
  trigger="$WORK/$mode.trigger"; devfile="$WORK/$mode.dev"
  rm -f "$trigger" "$devfile" "$WORK/$mode.out" "$WORK/$mode.trace"
  sudo -n env AHK_FIXTURE_NAME="combo-$mode-$$" AHK_FIXTURE_DEVPATH="$devfile" \
    "$FIXTURE" --script-trigger "$trigger" "${sequence[@]}" >"$WORK/$mode-fixture.log" 2>&1 &
  for _ in $(seq 1 100); do [ -s "$devfile" ] && break; sleep .03; done
  node=$(cat "$devfile"); for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .03; done
  sock="$WORK/$mode.sock"; sudo -n rm -f "$sock" "$sock.lock"
  sudo -n env AHK_INPUTD_TEST_DEVICE="$node" "$BROKER" --socket "$sock" --socket-mode 0666 -v >"$WORK/$mode-broker.log" 2>&1 &
  for _ in $(seq 1 120); do grep -q grabbed "$WORK/$mode-broker.log" 2>/dev/null && break; sleep .03; done
  ( cd "$WORK" && sudo -n env AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$sock" \
      AHK_INPUT_PIPELINE="$mode" AHK_INPUT_PIPELINE_TRACE="$WORK/$mode.trace" \
      "$AHK" combo.ahk "$WORK/$mode.out" "$WORK/$mode.ready" >"$WORK/$mode-ahk.log" 2>&1 ) & AP=$!
  for _ in $(seq 1 200); do [ -f "$WORK/$mode.ready" ] && grep -q subscribed "$WORK/$mode-broker.log" 2>/dev/null && break; sleep .03; done
  grep -q subscribed "$WORK/$mode-broker.log"
  touch "$trigger"; wait "$AP"
  grep -q "final mode=$mode total=7" "$WORK/$mode.out"
done
python3 "$ROOT/tests/oracle/verify_input_combo_pipeline.py" "$WORK" \
  "$OUT/input-combo-pipeline-summary.json"
echo "INPUT_COMBO_PIPELINE_ORACLE_PASS"
