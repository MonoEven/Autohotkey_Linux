#!/bin/bash
# Visible InputHook + one KeyOpt S must grab exactly that key family, while raw
# continues to observe all other keys. Independent XGrabKey probes are the
# oracle for suppression scope.
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
rm -f /tmp/scope_hook.ready /tmp/scope_hook.out /tmp/scope_target.txt
"$KEYCAP" -out /tmp/scope_target.txt -ms 15000 >/tmp/scope_target.log 2>&1 & TARGET=$!
sleep .3
cat >/tmp/scope_hook.ahk <<'EOF'
#Requires AutoHotkey v2.0
ih := InputHook("VT8", "{Enter}")
ih.Start()
ih.KeyOpt("{Enter}", "S")
FileAppend("ready`n", "/tmp/scope_hook.ready")
ih.Wait()
FileAppend("input=" ih.Input " reason=" ih.EndReason "`n", "/tmp/scope_hook.out")
ExitApp
EOF
"$BIN" /tmp/scope_hook.ahk >/tmp/scope_hook.log 2>&1 & PID=$!
for _ in $(seq 1 600); do test -f /tmp/scope_hook.ready && break; sleep .02; done
test -f /tmp/scope_hook.ready
test "$("$ORACLE" probe-grab-x11 Return)" = conflict
test "$("$ORACLE" probe-grab-x11 F7)" = free
"$ORACLE" inject-x11 g 25
"$ORACLE" inject-x11 Return 25
wait "$PID"
grep -q '^input=g reason=EndKey$' /tmp/scope_hook.out
sleep .2
grep -q 'k:down:g:.*:syn:0$' /tmp/scope_target.txt
if grep -q 'k:down:Return:' /tmp/scope_target.txt; then
  echo "suppressed Return reached target" >&2
  exit 1
fi
# Runtime reconfiguration: suppress F7, release Enter. KeyOpt must reconcile
# without restarting the hook.
cat >/tmp/scope_reconfig.ahk <<'EOF'
#Requires AutoHotkey v2.0
ih := InputHook("VT8")
ih.Start()
ih.KeyOpt("{Enter}", "S")
Sleep(500)
ih.KeyOpt("{Enter}", "V")
ih.KeyOpt("{F7}", "S")
FileAppend("ready`n", "/tmp/scope_reconfig.ready")
ih.Wait()
EOF
rm -f /tmp/scope_reconfig.ready
"$BIN" /tmp/scope_reconfig.ahk >/tmp/scope_reconfig.log 2>&1 & PID=$!
for _ in $(seq 1 600); do test -f /tmp/scope_reconfig.ready && break; sleep .02; done
test -f /tmp/scope_reconfig.ready
test "$("$ORACLE" probe-grab-x11 Return)" = free
test "$("$ORACLE" probe-grab-x11 F7)" = conflict
kill "$PID" "$TARGET" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
wait "$TARGET" 2>/dev/null || true
cat >"$OUT/suppression-scope-summary.json" <<'EOF'
{"schema":1,"result":"pass","initial":{"Return":"grabbed_and_suppressed","F7":"free","g":"raw_and_target_visible"},"reconfigured":{"Return":"free","F7":"grabbed"}}
EOF
echo "SUPPRESSION_SCOPE_ORACLE_PASS enter_only=1 runtime_reconfigure=1"
