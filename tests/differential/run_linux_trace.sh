#!/bin/bash
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a bash "$0" "$BIN"
fi
OUT="$ROOT/tests/differential/out"
ORACLE_OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT" "$ORACLE_OUT"
INJECTOR="$ORACLE_OUT/input-oracle"
${CC:-cc} -O2 -Wall -Wextra "$ROOT/tests/oracle/input_oracle.c" -o "$INJECTOR" \
  $(pkg-config --cflags --libs x11 xi xtst)
ACTUAL="$OUT/linux-v2-x64.jsonl"
CONTROL=/tmp/ahk_differential_control
LOG=/tmp/ahk_differential_runtime.log
rm -f "$ACTUAL" "$CONTROL" "$LOG"

wait_phase() {
  phase="$1"
  loops=0
  while [ "$loops" -lt 500 ]; do
    grep -qx "$phase" "$CONTROL" 2>/dev/null && return 0
    sleep .02
    loops=$((loops + 1))
  done
  echo "DIFFERENTIAL_PHASE_TIMEOUT phase=$phase" >&2
  return 1
}

"$BIN" "$ROOT/tests/differential/trace_suite.ahk" "$ACTUAL" "$CONTROL" >"$LOG" 2>&1 &
runtime=$!
cleanup() {
  kill "$runtime" 2>/dev/null || true
  wait "$runtime" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

wait_phase input || { cat "$LOG"; exit 1; }
"$INJECTOR" inject-keycode-x11 38 25
"$INJECTOR" inject-keycode-state-x11 50 down
"$INJECTOR" inject-keycode-x11 38 25
"$INJECTOR" inject-keycode-state-x11 50 up
"$INJECTOR" inject-keycode-x11 191 25
wait_phase hotkeys || { cat "$LOG"; exit 1; }
"$INJECTOR" inject-keycode-state-x11 37 down
"$INJECTOR" inject-keycode-x11 95 25
"$INJECTOR" inject-keycode-state-x11 37 up
sleep .2
"$INJECTOR" inject-keycode-state-x11 96 down
sleep .5
"$INJECTOR" inject-keycode-state-x11 96 up
wait_phase hotstring || { cat "$LOG"; exit 1; }
"$INJECTOR" inject-keycode-x11 52 25
"$INJECTOR" inject-keycode-x11 53 25
"$INJECTOR" inject-keycode-x11 24 25
wait_phase wildcard || { cat "$LOG"; exit 1; }
"$INJECTOR" inject-keycode-state-x11 50 down
"$INJECTOR" inject-keycode-x11 76 40
"$INJECTOR" inject-keycode-state-x11 50 up
wait_phase case-hotstring || { cat "$LOG"; exit 1; }
# Lowercase zxc must not fire the case-sensitive trigger; zXc must fire once.
"$INJECTOR" inject-keycode-x11 52 25
"$INJECTOR" inject-keycode-x11 53 25
"$INJECTOR" inject-keycode-x11 54 25
"$INJECTOR" inject-keycode-x11 65 25
"$INJECTOR" inject-keycode-x11 52 25
"$INJECTOR" inject-keycode-state-x11 50 down
"$INJECTOR" inject-keycode-x11 53 25
"$INJECTOR" inject-keycode-state-x11 50 up
"$INJECTOR" inject-keycode-x11 54 25

wait "$runtime"; rc=$?
trap - EXIT HUP INT TERM
[ "$rc" = 0 ] || { echo "DIFFERENTIAL_RUNTIME_FAIL rc=$rc"; cat "$LOG"; exit 1; }
python3 "$ROOT/tests/differential/compare_trace.py" \
  "$ROOT/tests/differential/golden/windows-v2.0.26-x64.jsonl" "$ACTUAL" \
  --summary "$OUT/linux-vs-windows-summary.json"
