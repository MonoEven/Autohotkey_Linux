#!/bin/bash
# Bidirectional external oracle:
#   AHK Send -> independent XI2 JSONL recorder
#   independent XTEST injector -> AHK hotkey capture
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
ORACLE="$OUT/input-oracle"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/input_oracle.c" -o "$ORACLE" \
  $(pkg-config --cflags --libs x11 xi xtst)

TRACE=/tmp/ahk_oracle_send.jsonl
SUMMARY="$OUT/x11-send-summary.json"
rm -f "$TRACE" "$SUMMARY" /tmp/ahk_oracle_capture_ready /tmp/ahk_oracle_capture_pass

"$ORACLE" record-x11 "$TRACE" F7 2 8000 >/tmp/ahk_oracle_recorder.log 2>&1 &
REC_PID=$!
for _ in $(seq 1 100); do
  grep -q '"type":"ready"' "$TRACE" 2>/dev/null && break
  sleep 0.02
done
grep -q '"type":"ready"' "$TRACE"
cat >/tmp/ahk_oracle_send.ahk <<'EOF'
#Requires AutoHotkey v2.0
SetKeyDelay(80, 20)
Sleep(100)
SendEvent("{F7 down}{F7 up}")
ExitApp
EOF
"$BIN" /tmp/ahk_oracle_send.ahk
wait "$REC_PID"
python3 "$ROOT/tests/oracle/verify_trace.py" "$TRACE" | tee "$SUMMARY"

cat >/tmp/ahk_oracle_capture.ahk <<'EOF'
#Requires AutoHotkey v2.0
FileDelete("/tmp/ahk_oracle_capture_pass")
OnF8(*) {
    FileAppend("external-injector-fired`n", "/tmp/ahk_oracle_capture_pass")
    ExitApp
}
Hotkey("F8", OnF8)
FileAppend("ready`n", "/tmp/ahk_oracle_capture_ready")
SetTimer(() => ExitApp(3), -8000)
EOF
"$BIN" /tmp/ahk_oracle_capture.ahk >/tmp/ahk_oracle_capture.log 2>&1 &
AHK_PID=$!
for _ in $(seq 1 100); do
  test -f /tmp/ahk_oracle_capture_ready && break
  sleep 0.02
done
test -f /tmp/ahk_oracle_capture_ready
"$ORACLE" inject-x11 F8 30
wait "$AHK_PID"
grep -q '^external-injector-fired$' /tmp/ahk_oracle_capture_pass

echo "INPUT_ORACLE_PASS trace=$TRACE summary=$SUMMARY"
