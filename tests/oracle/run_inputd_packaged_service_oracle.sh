#!/bin/bash
# Real installed-package ahk-inputd oracle. Run after deb/RPM/tar
# --inputd-service installation on a systemd VM; the calling user must be a
# member of input. It proves the actual core client discovers /run without an
# AHK_INPUTD_SOCKET override and that the packaged daemon releases grabs idle.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
BIN="${1:-/usr/bin/ahk}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
SOCK=/run/ahk-inputd.sock
SERVICE=ahk-inputd.service
SOCKET_UNIT=ahk-inputd.socket
SUDO=()
[ "$(id -u)" = 0 ] || SUDO=(sudo -n)
"${SUDO[@]}" true
command -v systemctl >/dev/null
[ -x "$BIN" ]
[ -f /usr/lib/systemd/system/ahk-inputd.service ] \
  || [ -f /etc/systemd/system/ahk-inputd.service ]
[ -f /usr/lib/systemd/system/ahk-inputd.socket ] \
  || [ -f /etc/systemd/system/ahk-inputd.socket ]
"${SUDO[@]}" systemctl enable --now "$SOCKET_UNIT" >/dev/null
"${SUDO[@]}" systemctl stop "$SERVICE" >/dev/null 2>&1 || true
[ "$(systemctl is-active "$SOCKET_UNIT")" = active ]
[ "$(systemctl show -p MainPID --value "$SERVICE")" = 0 ]
[ -S "$SOCK" ]
[ "$(stat -c %a "$SOCK")" = 660 ]
[ "$(stat -c %U "$SOCK")" = root ]
[ "$(stat -c %G "$SOCK")" = input ]
[ -w "$SOCK" ] || { echo "INPUTD_PACKAGED_SKIP reason=caller-not-in-input-group"; exit 0; }
inode=$(stat -c %i "$SOCK")

PROBE=/tmp/ahk-inputd-packaged-probe.ahk
RESULT=/tmp/ahk-inputd-packaged-result.txt
LOG=/tmp/ahk-inputd-packaged-core.log
cat >"$PROBE" <<'EOF'
#Requires AutoHotkey v2.0
Hotkey("F12", (*) => 0)
FileAppend("backend=" A_HotkeyBackend "`n", A_Args[1])
SetTimer(() => ExitApp(), -800)
EOF
run_core() {
  rm -f "$RESULT" "$LOG"
  env -u AHK_INPUTD_SOCKET -u AHK_INPUTD_DISABLE \
    AHK_INPUT_BACKEND=evdev "$BIN" "$PROBE" "$RESULT" >"$LOG" 2>&1
  grep -q '^backend=evdev$' "$RESULT"
  grep -q '^\[evdev\] broker mode active$' "$LOG"
}
start_epoch=$(date +%s)
run_core
pid_first=$(systemctl show -p MainPID --value "$SERVICE")
[ "$pid_first" -gt 0 ]
[ "$(stat -c %i "$SOCK")" = "$inode" ]
for _ in $(seq 1 160); do
  [ "$(systemctl show -p MainPID --value "$SERVICE")" = 0 ] && break
  sleep .05
done
[ "$(systemctl show -p MainPID --value "$SERVICE")" = 0 ]
[ "$(systemctl is-active "$SOCKET_UNIT")" = active ]
[ "$(stat -c %i "$SOCK")" = "$inode" ]

# A second real core invocation must demand-start a different daemon PID.
run_core
pid_second=$(systemctl show -p MainPID --value "$SERVICE")
[ "$pid_second" -gt 0 ]
[ "$pid_second" != "$pid_first" ]
"${SUDO[@]}" journalctl -u "$SERVICE" --since "@$start_epoch" --no-pager \
  > /tmp/ahk-inputd-packaged-journal.log
[ "$(grep -c 'adopted systemd socket /run/ahk-inputd.sock' /tmp/ahk-inputd-packaged-journal.log)" -ge 2 ]
grep -Eq 'client pid=[0-9]+ uid=[0-9]+ gid=[0-9]+ connected' \
  /tmp/ahk-inputd-packaged-journal.log

# Wait out the second client and independently prove an input device can be
# grabbed again (the root broker no longer owns it).
for _ in $(seq 1 160); do
  [ "$(systemctl show -p MainPID --value "$SERVICE")" = 0 ] && break
  sleep .05
done
[ "$(systemctl show -p MainPID --value "$SERVICE")" = 0 ]
CC="${CC:-cc}"
WATCH="$OUT/inputd-packaged-watch"
$CC -O2 -Wall -Wextra -o "$WATCH" "$ROOT/tests/oracle/inputd_watch.c"
[ "$("${SUDO[@]}" "$WATCH" probe)" = GRAB_OK ]

cat >"$OUT/inputd-packaged-service-summary.json" <<EOF
{"schema":1,"result":"pass","socket":"/run/ahk-inputd.sock","owner":"root:input","mode":"0660","core_auto_discovery":true,"broker_mode":true,"demand_starts":2,"idle_exit":true,"grab_recovery":true,"socket_inode":$inode,"pid_first":$pid_first,"pid_second":$pid_second}
EOF
rm -f "$PROBE" "$RESULT" "$LOG"
echo "INPUTD_PACKAGED_SERVICE_ORACLE_PASS pids=$pid_first,$pid_second inode=$inode"
