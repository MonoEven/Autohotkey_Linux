#!/bin/bash
# Validate AhkInputEvent v1 across self and other-process XTEST sources.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
ORACLE="$OUT/input-oracle"
TRACE=/tmp/ahk_input_event_v1.jsonl
SUMMARY="$OUT/input-event-v1-summary.json"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/input_oracle.c" -o "$ORACLE" \
  $(pkg-config --cflags --libs x11 xi xtst)
rm -f "$TRACE" "$SUMMARY" /tmp/input_event.ready /tmp/input_event.self_done
cat >/tmp/input_event_model.ahk <<'EOF'
#Requires AutoHotkey v2.0
Hotkey("F12", (*) => 0) ; materialize and pump the X11 backend.
FileAppend("ready`n", "/tmp/input_event.ready")
SendLevel(3)
SendEvent("a")
SendLevel(0)
FileAppend("done`n", "/tmp/input_event.self_done")
Sleep(1200)
ExitApp
EOF
AHK_INPUT_EVENT_TRACE="$TRACE" "$BIN" /tmp/input_event_model.ahk >/tmp/input_event_model.log 2>&1 & PID=$!
for _ in $(seq 1 600); do test -f /tmp/input_event.self_done && break; sleep .02; done
test -f /tmp/input_event.self_done
"$ORACLE" inject-x11 b 25
wait "$PID"
python3 "$ROOT/tests/oracle/verify_input_event_trace.py" "$TRACE" | tee "$SUMMARY"
echo "INPUT_EVENT_MODEL_ORACLE_PASS summary=$SUMMARY"
