#!/bin/bash
# M6 fault injection: kill and replace the GlobalShortcuts portal service while
# one AHK process remains alive. The runtime must recreate and rebind its session.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
command -v dbus-run-session >/dev/null || { echo PORTAL_RESTART_SKIP dbus-run-session-missing; exit 2; }
${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/portal-restart-probe" \
  "$ROOT/tests/oracle/portal_restart_probe.c" $(pkg-config --cflags --libs dbus-1) || exit 2
cat >/tmp/portal_restart.ahk <<'EOF'
#Requires AutoHotkey v2.0
global portalCount := 0
^F12::{
    global portalCount
    portalCount += 1
    FileAppend(portalCount "`n", "/tmp/portal_restart_fired")
    if portalCount = 2
        ExitApp
}
route := HotkeyBackendGet("^F12")
FileAppend("backend=" route.backend "`n", "/tmp/portal_restart_ahk_ready")
SetTimer(() => ExitApp(9), -30000)
EOF
rm -f /tmp/portal_restart_{ahk_ready,fired,ready1,ready2,bound1,bound2,log1,log2,ahk.log,session.log}
export ROOT BIN OUT
dbus-run-session -- bash -c '
  set -u
  p1= p2= ahk=
  cleanup() {
    test -z "$ahk" || kill "$ahk" 2>/dev/null || true
    test -z "$p1" || kill "$p1" 2>/dev/null || true
    test -z "$p2" || kill "$p2" 2>/dev/null || true
    test -z "$ahk" || wait "$ahk" 2>/dev/null || true
    test -z "$p1" || wait "$p1" 2>/dev/null || true
    test -z "$p2" || wait "$p2" 2>/dev/null || true
  }
  trap cleanup EXIT HUP INT TERM
  wait_file() {
    file=$1
    loops=$2
    i=0
    while [ "$i" -lt "$loops" ]; do
      test -s "$file" && return 0
      sleep .05
      i=$((i + 1))
    done
    return 1
  }
  wait_line() {
    file=$1
    line=$2
    loops=$3
    i=0
    while [ "$i" -lt "$loops" ]; do
      grep -qx "$line" "$file" 2>/dev/null && return 0
      sleep .05
      i=$((i + 1))
    done
    return 1
  }

  "$OUT/portal-restart-probe" /tmp/portal_restart_ready1 /tmp/portal_restart_bound1 /tmp/portal_restart_log1 &
  p1=$!
  wait_file /tmp/portal_restart_ready1 100 || exit 10
  XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-fake XDG_CURRENT_DESKTOP=KDE \
    AHK_INPUT_BACKEND=portal "$BIN" /tmp/portal_restart.ahk >/tmp/portal_restart_ahk.log 2>&1 &
  ahk=$!
  wait_file /tmp/portal_restart_ahk_ready 100 || exit 11
  grep -q "^backend=portal$" /tmp/portal_restart_ahk_ready || exit 12
  wait_file /tmp/portal_restart_bound1 300 || exit 13
  kill -USR1 "$p1"
  wait_line /tmp/portal_restart_fired 1 100 || exit 14

  # Actual owner disappearance followed by a new process/name owner.
  kill -TERM "$p1"
  wait "$p1" || true
  p1=
  sleep .2
  "$OUT/portal-restart-probe" /tmp/portal_restart_ready2 /tmp/portal_restart_bound2 /tmp/portal_restart_log2 &
  p2=$!
  wait_file /tmp/portal_restart_ready2 100 || exit 15
  wait_file /tmp/portal_restart_bound2 300 || exit 16
  kill -USR1 "$p2"
  wait_line /tmp/portal_restart_fired 2 100 || exit 17
  wait "$ahk" || exit 18
  ahk=
  kill -TERM "$p2"
  wait "$p2" || true
  p2=
' >/tmp/portal_restart_session.log 2>&1
rc=$?
[ "$rc" = 0 ] \
  || { echo "PORTAL_RESTART_ORACLE_FAIL rc=$rc"; cat /tmp/portal_restart_session.log /tmp/portal_restart_ahk.log /tmp/portal_restart_log1 /tmp/portal_restart_log2; exit 1; }
[ "$(grep -c '^create$' /tmp/portal_restart_log1)" = 1 ] \
  && [ "$(grep -c '^bind=' /tmp/portal_restart_log1)" = 1 ] \
  && [ "$(grep -c '^activate=' /tmp/portal_restart_log1)" = 1 ] \
  && [ "$(grep -c '^create$' /tmp/portal_restart_log2)" = 1 ] \
  && [ "$(grep -c '^bind=' /tmp/portal_restart_log2)" = 1 ] \
  && [ "$(grep -c '^activate=' /tmp/portal_restart_log2)" = 1 ] \
  && [ "$(tr '\n' ',' </tmp/portal_restart_fired)" = '1,2,' ] \
  || { echo PORTAL_RESTART_TRACE_FAIL; cat /tmp/portal_restart_log1 /tmp/portal_restart_log2 /tmp/portal_restart_fired; exit 1; }
id1=$(sed -n 's/^bind=//p' /tmp/portal_restart_log1)
id2=$(sed -n 's/^bind=//p' /tmp/portal_restart_log2)
[ -n "$id1" ] && [ "$id1" = "$id2" ] \
  || { echo PORTAL_RESTART_ID_FAIL; cat /tmp/portal_restart_log1 /tmp/portal_restart_log2; exit 1; }
cat >"$OUT/portal-restart-summary.json" <<EOF
{"schema":1,"result":"pass","fault":"portal-owner-restart","runtime_pid_stable":true,"create_sessions":2,"binds":2,"activations":2,"shortcut_id":"$id1"}
EOF
echo "PORTAL_RESTART_ORACLE_PASS create=2 bind=2 activated=2 runtime_pid_stable=1"
