#!/bin/bash
# Two processes observe one XI2 stream without competing all-key grabs.
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
rm -f /tmp/raw_hs_{1,2}.{ready,out} /tmp/raw_hs_target.txt
cat >/tmp/raw_multiscript_hs.ahk <<'EOF'
#Requires AutoHotkey v2.0
id := A_Args[1]
trigger := id = "1" ? "u" : "v"
OnTrigger(*) {
    global id
    FileAppend("fired`n", "/tmp/raw_hs_" id ".out")
    ExitApp
}
; B0: observe/execute without editing target text. *: no end char required.
Hotstring(":B0X*:" trigger, OnTrigger)
FileAppend("ready`n", "/tmp/raw_hs_" id ".ready")
SetTimer(() => ExitApp(3), -10000)
EOF
"$KEYCAP" -out /tmp/raw_hs_target.txt -ms 14000 >/tmp/raw_hs_target.log 2>&1 & TARGET=$!
sleep .3
"$BIN" /tmp/raw_multiscript_hs.ahk 1 >/tmp/raw_hs_1.log 2>&1 & P1=$!
"$BIN" /tmp/raw_multiscript_hs.ahk 2 >/tmp/raw_hs_2.log 2>&1 & P2=$!
for _ in $(seq 1 600); do
  test -f /tmp/raw_hs_1.ready && test -f /tmp/raw_hs_2.ready && break
  sleep .02
done
test -f /tmp/raw_hs_1.ready; test -f /tmp/raw_hs_2.ready
"$ORACLE" inject-x11 u 25
sleep .15
"$ORACLE" inject-x11 space 25
sleep .15
"$ORACLE" inject-x11 v 25
wait "$P1"; wait "$P2"
grep -q '^fired$' /tmp/raw_hs_1.out
grep -q '^fired$' /tmp/raw_hs_2.out
sleep .2
grep -q 'k:down:u:.*:syn:0$' /tmp/raw_hs_target.txt
grep -q 'k:down:v:.*:syn:0$' /tmp/raw_hs_target.txt
kill "$TARGET" 2>/dev/null || true; wait "$TARGET" 2>/dev/null || true
cat >"$OUT/multiscript-hotstring-summary.json" <<'EOF'
{"schema":1,"result":"pass","processes":2,"badaccess":false,"target_original_events":true}
EOF
echo "MULTISCRIPT_HOTSTRING_ORACLE_PASS processes=2 target_original=1"
