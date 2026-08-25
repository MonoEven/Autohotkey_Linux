#!/bin/bash
# Same-process ahk-inputd restart oracle. Requires the packaged systemd service,
# an input-group caller and uinput access. A single AHK PID fires F12, survives
# SIGKILL of the root broker, reconnects/re-subscribes within 5 seconds and
# fires F12 again from the same independent uinput device.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
BIN="${1:-/usr/bin/ahk}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
CC="${CC:-cc}"
INJECT="$OUT/inputd-reconnect-inject"
$CC -O2 -Wall -Wextra -o "$INJECT" "$ROOT/tools/linux/uinput-inject.c"
SUDO=()
[ "$(id -u)" = 0 ] || SUDO=(sudo -n)
"${SUDO[@]}" true
systemctl is-active --quiet ahk-inputd.socket
[ -w /run/ahk-inputd.sock ] || {
  echo "INPUTD_RECONNECT_SKIP reason=caller-not-in-input-group"
  exit 0
}
CMD=/tmp/ahk-inputd-reconnect.cmd
SCRIPT=/tmp/ahk-inputd-reconnect.ahk
READY=/tmp/ahk-inputd-reconnect.ready
FIRES=/tmp/ahk-inputd-reconnect.fires
AHK_LOG=/tmp/ahk-inputd-reconnect-ahk.log
INJECT_LOG=/tmp/ahk-inputd-reconnect-inject.log
AHK_PID= INJECT_PID=
cleanup() {
  [ -n "$AHK_PID" ] && kill "$AHK_PID" 2>/dev/null || true
  [ -n "$INJECT_PID" ] && kill "$INJECT_PID" 2>/dev/null || true
  [ -n "$AHK_PID" ] && wait "$AHK_PID" 2>/dev/null || true
  [ -n "$INJECT_PID" ] && wait "$INJECT_PID" 2>/dev/null || true
  rm -f "$CMD" "$SCRIPT" "$READY" "$FIRES" "$AHK_LOG" "$INJECT_LOG"
}
trap cleanup EXIT HUP INT TERM
cleanup
cat >"$SCRIPT" <<'EOF'
#Requires AutoHotkey v2.0
fires := 0
OnF12(*) {
    global fires
    fires += 1
    FileAppend("fire=" fires " pid=" DllCall("getpid", "Int") "`n", "/tmp/ahk-inputd-reconnect.fires")
    if fires = 2
        ExitApp
}
Hotkey("F12", OnF12)
FileAppend("ready pid=" DllCall("getpid", "Int") "`n", "/tmp/ahk-inputd-reconnect.ready")
SetTimer(() => ExitApp(9), -20000)
EOF
"$INJECT" "$CMD" >"$INJECT_LOG" 2>&1 &
INJECT_PID=$!
sleep .4
env -u AHK_INPUTD_SOCKET -u AHK_INPUTD_DISABLE AHK_INPUT_BACKEND=evdev \
  "$BIN" "$SCRIPT" >"$AHK_LOG" 2>&1 &
AHK_PID=$!
for _ in $(seq 1 200); do
  [ -f "$READY" ] && grep -q 'broker mode active' "$AHK_LOG" && break
  sleep .05
done
[ -f "$READY" ]
grep -q 'broker mode active' "$AHK_LOG"
recorded_pid=$(sed -n 's/^ready pid=//p' "$READY")
[ "$recorded_pid" = "$AHK_PID" ]
# Let the broker rescan and grab the independent uinput keyboard.
sleep 1.6
echo '88 tap' >"$CMD"
for _ in $(seq 1 100); do
  fire_count=0
  [ -f "$FIRES" ] && fire_count=$(grep -c '^fire=' "$FIRES" || true)
  [ "${fire_count:-0}" -ge 1 ] && break
  sleep .05
done
[ "$(grep -c '^fire=1 ' "$FIRES")" = 1 ]
old_broker=$(systemctl show -p MainPID --value ahk-inputd.service)
[ "$old_broker" -gt 0 ]
"${SUDO[@]}" systemctl kill --kill-whom=main --signal=KILL ahk-inputd.service
for _ in $(seq 1 120); do
  new_broker=$(systemctl show -p MainPID --value ahk-inputd.service)
  grep -q 'broker reconnected' "$AHK_LOG" 2>/dev/null \
    && [ "$new_broker" -gt 0 ] 2>/dev/null \
    && [ "$new_broker" != "$old_broker" ] && break
  sleep .05
done
new_broker=$(systemctl show -p MainPID --value ahk-inputd.service)
[ "$new_broker" -gt 0 ]
[ "$new_broker" != "$old_broker" ]
grep -q 'broker disconnected; retrying for 5000ms' "$AHK_LOG"
grep -q 'broker reconnected' "$AHK_LOG"
# New daemon must rescan/regrab this still-live uinput keyboard.
sleep 1.6
echo '88 tap' >"$CMD"
for _ in $(seq 1 120); do
  fire_count=0
  [ -f "$FIRES" ] && fire_count=$(grep -c '^fire=' "$FIRES" || true)
  [ "${fire_count:-0}" -ge 2 ] && break
  sleep .05
done
wait "$AHK_PID"
AHK_PID=
[ "$(grep -c '^fire=1 ' "$FIRES")" = 1 ]
[ "$(grep -c '^fire=2 ' "$FIRES")" = 1 ]
[ "$(awk '{print $2}' "$FIRES" | sort -u | wc -l)" = 1 ]
grep -q "pid=$recorded_pid" "$FIRES"
cat >"$OUT/inputd-reconnect-summary.json" <<EOF
{"schema":1,"result":"pass","same_ahk_pid":$recorded_pid,"broker_pid_before":$old_broker,"broker_pid_after":$new_broker,"reconnect_budget_ms":5000,"retry_interval_ms":500,"rules_resubscribed":true,"fires_before":1,"fires_after":1}
EOF
trap - EXIT HUP INT TERM
cleanup
echo "INPUTD_RECONNECT_ORACLE_PASS ahk_pid=$recorded_pid brokers=$old_broker,$new_broker"
