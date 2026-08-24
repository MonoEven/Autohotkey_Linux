#!/bin/bash
# Prove visible InputHook is a multi-client XI2 observer, not an all-key grab.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
ORACLE="$OUT/input-oracle"
KEYCAP="$OUT/xkeycap"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/input_oracle.c" -o "$ORACLE" \
  $(pkg-config --cflags --libs x11 xi xtst)
cc -O2 -Wall -Wextra "$ROOT/tests/doccheck/xkeycap.c" -o "$KEYCAP" \
  $(pkg-config --cflags --libs x11)
rm -f /tmp/raw_hook_{1,2}.ready /tmp/raw_hook_{1,2}.out /tmp/raw_hook_target.txt
cat >/tmp/raw_visible_hook.ahk <<'EOF'
#Requires AutoHotkey v2.0
id := A_Args[1]
ih := InputHook("VL1T6")
ih.Start()
FileAppend("ready`n", "/tmp/raw_hook_" id ".ready")
ih.Wait()
FileAppend("input=" ih.Input "`n", "/tmp/raw_hook_" id ".out")
ExitApp
EOF
"$KEYCAP" -out /tmp/raw_hook_target.txt -ms 12000 >/tmp/raw_hook_target.log 2>&1 &
TARGET_PID=$!
sleep .3
"$BIN" /tmp/raw_visible_hook.ahk 1 >/tmp/raw_hook_1.log 2>&1 & P1=$!
"$BIN" /tmp/raw_visible_hook.ahk 2 >/tmp/raw_hook_2.log 2>&1 & P2=$!
for _ in $(seq 1 600); do
  test -f /tmp/raw_hook_1.ready && test -f /tmp/raw_hook_2.ready && break
  sleep .02
done
test -f /tmp/raw_hook_1.ready
test -f /tmp/raw_hook_2.ready
"$ORACLE" inject-x11 g 25
wait "$P1"
wait "$P2"
grep -q '^input=g$' /tmp/raw_hook_1.out
grep -q '^input=g$' /tmp/raw_hook_2.out
sleep .2
grep -q 'k:down:g:.*:syn:0$' /tmp/raw_hook_target.txt
kill "$TARGET_PID" 2>/dev/null || true
wait "$TARGET_PID" 2>/dev/null || true
cat >"$OUT/raw-capture-summary.json" <<'EOF'
{"schema":1,"result":"pass","observers":2,"target_visible":true,"all_key_grab":false,"input":"g"}
EOF
echo "RAW_CAPTURE_ORACLE_PASS observers=2 target_visible=1"
