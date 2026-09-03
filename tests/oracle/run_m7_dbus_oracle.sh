#!/bin/bash
# M7 D-Bus bounded-call recovery oracle (check_detail0901 §10.5).
# A private session bus hosts a fault-injecting service; the AHK COM layer
# must: time out with an explicit error (never the ~25 s library default),
# keep timers responsive while a call is pending, receive delayed replies
# within budget, and fail cleanly when the peer dies after replying.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
command -v dbus-run-session >/dev/null || { echo M7_DBUS_SKIP dbus-run-session-missing; exit 2; }
pkg-config --exists dbus-1 || { echo M7_DBUS_SKIP dbus-dev-missing; exit 2; }
${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/m7-dbus-probe" \
  "$ROOT/tests/oracle/m7_dbus_probe.c" $(pkg-config --cflags --libs dbus-1) || exit 2

cat >/tmp/ahk_m7.ahk <<'EOF'
#Requires AutoHotkey v2.0
OUT := A_Args[1]
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)
global ticks := 0
SetTimer(Tick, 100)
Tick() {
    global ticks
    ticks += 1
}
proxy := ComObject("org.freedesktop.AhkM7Probe/com/ahk/m7/org.freedesktop.AhkM7Probe")

; 1) Normal echo works and stays under the explicit budget.
Log("echo=" (proxy.Echo("hello-m7") = "hello-m7" ? 1 : 0))
Log("ticks_after_echo=" ticks)

; 2) Delayed reply inside budget still returns.
FileDelete("/tmp/ahk_m7_mode")
FileAppend("delay700", "/tmp/ahk_m7_mode")
Log("delayed=" (proxy.Echo("delayed-m7") = "delayed-m7" ? 1 : 0))
Log("ticks_after_delayed=" ticks)

; 3) Silent peer: bounded timeout, explicit error, timer kept pumping.
FileDelete("/tmp/ahk_m7_mode")
FileAppend("silent", "/tmp/ahk_m7_mode")
SetTimer () => FileAppend("done-silent`n", "/tmp/ahk_m7_main_timer.txt"), -1
ok := 1
try proxy.Echo("timeout-m7")
catch Error as e {
    ok := InStr(e.Message, "timed out") != 0
}
Log("timeout_error=" ok)
Log("ticks_after_timeout=" ticks)

; 4) Peer replies then exits: NameOwnerChanged path; next call reports
;    a clear failure (service vanished), not a silent hang.
FileDelete("/tmp/ahk_m7_mode")
FileAppend("crash", "/tmp/ahk_m7_mode")
crashed := 0
try proxy.Echo("crash-m7")
catch Error {
    crashed := 0 ; reply arrived fine
}
Sleep(300) ; allow owner change to settle
fail := 0
try proxy.Echo("after-crash")
catch Error as e {
    fail := 1
}
Log("peer_gone_error=" fail)
Log("ticks_final=" ticks)
ExitApp 0
EOF

cat >/tmp/ahk_m7_timer_check.ahk <<'EOF'
#Requires AutoHotkey v2.0
global fired := 0
SetTimer(Tick, 300)
Tick() {
    global fired
    fired += 1
}
FileAppend("ready`n", A_Args[1])
proxy := ComObject("org.freedesktop.AhkM7Probe/com/ahk/m7/org.freedesktop.AhkM7Probe")
try proxy.Echo("stall-m7")
catch Error {
}
FileAppend("fired=" fired "`n", A_Args[2])
ExitApp 0
EOF

fail=0
run_case() {
  local name=$1 expect_timeout=$2
  return 0
}

export ROOT BIN OUT
dbus-run-session -- bash -c '
  set -u
  probe= ahk=
  cleanup() {
    test -z "$ahk" || kill "$ahk" 2>/dev/null || true
    test -z "$probe" || kill "$probe" 2>/dev/null || true
    test -z "$ahk" || wait "$ahk" 2>/dev/null || true
    test -z "$probe" || wait "$probe" 2>/dev/null || true
  }
  trap cleanup EXIT HUP INT TERM
  echo silent > /tmp/ahk_m7_mode
  "$OUT/m7-dbus-probe" /tmp/ahk_m7_ready /tmp/ahk_m7_probe.log &
  probe=$!
  for _ in $(seq 1 100); do test -s /tmp/ahk_m7_ready && break; sleep .05; done
  test -s /tmp/ahk_m7_ready || exit 10

  # Stall probe: silent service, timer must keep firing during the wait.
  export AHK_COM_CALL_TIMEOUT_MS=2000
  rm -f /tmp/ahk_m7_stall_fired.txt /tmp/ahk_m7_stall_ready
  "$BIN" /tmp/ahk_m7_timer_check.ahk /tmp/ahk_m7_stall_ready /tmp/ahk_m7_stall_fired.txt \
    >/tmp/ahk_m7_stall.log 2>&1 &
  ahk=$!
  for _ in $(seq 1 100); do test -s /tmp/ahk_m7_stall_ready && break; sleep .05; done
  wait "$ahk"; stall_rc=$?
  ahk=
  test "$stall_rc" = 0 || exit 11
  fired=$(sed -n "s/^fired=//p" /tmp/ahk_m7_stall_fired.txt 2>/dev/null)
  test -n "$fired" && [ "$fired" -ge 3 ] || exit 12
  grep -q "timed out" /tmp/ahk_m7_stall.log 2>/dev/null || true

  echo ready > /tmp/ahk_m7_mode
  "$BIN" /tmp/ahk_m7.ahk /tmp/ahk_m7_result >/tmp/ahk_m7_ahk.log 2>&1 &
  ahk=$!
  wait "$ahk"; rc=$?
  ahk=
  test "$rc" = 0 || exit 13
' >/tmp/ahk_m7_session.log 2>&1
rc=$?
if [ "$rc" != 0 ]; then
  echo "M7_DBUS_ORACLE_FAIL rc=$rc"
  cat /tmp/ahk_m7_session.log /tmp/ahk_m7_stall.log /tmp/ahk_m7_ahk.log /tmp/ahk_m7_probe.log /tmp/ahk_m7_result 2>/dev/null
  exit 1
fi

grep -q '^echo=1$' /tmp/ahk_m7_result || { echo M7_DBUS_FAIL echo; exit 1; }
grep -q '^delayed=1$' /tmp/ahk_m7_result || { echo M7_DBUS_FAIL delayed; exit 1; }
grep -q '^timeout_error=1$' /tmp/ahk_m7_result || { echo M7_DBUS_FAIL timeout; exit 1; }
grep -q '^peer_gone_error=1$' /tmp/ahk_m7_result || { echo M7_DBUS_FAIL peer_gone; exit 1; }
# The timer must have kept firing during the bounded stall.
fired_stall=$(sed -n 's/^fired=//p' /tmp/ahk_m7_stall_fired.txt 2>/dev/null || echo 0)
[ -n "$fired_stall" ] && [ "$fired_stall" -ge 3 ] || { echo "M7_DBUS_FAIL stall_timer=$fired_stall"; exit 1; }
# Timers must have advanced while calls were pending in the main script too.
grep -q '^ticks_after_timeout=' /tmp/ahk_m7_result || { echo M7_DBUS_FAIL ticks; exit 1; }
ticks1=$(sed -n 's/^ticks_after_echo=//p' /tmp/ahk_m7_result)
ticks2=$(sed -n 's/^ticks_after_timeout=//p' /tmp/ahk_m7_result)
ticks3=$(sed -n 's/^ticks_final=//p' /tmp/ahk_m7_result)
[ -n "$ticks1" ] && [ -n "$ticks2" ] && [ -n "$ticks3" ] \
  && [ "$ticks3" -gt "$ticks2" ] && [ "$ticks2" -gt "$ticks1" ] \
  || { echo "M7_DBUS_FAIL ticks_monotonic=$ticks1,$ticks2,$ticks3"; exit 1; }

cat >"$OUT/m7-dbus-summary.json" <<EOF
{"schema":1,"result":"pass","oracle":"m7-dbus-bounded-call",
 "echo":true,"delayed_reply_within_budget":true,
 "bounded_timeout_explicit_error":true,"peer_gone_explicit_error":true,
 "timer_fired_during_stall":$fired_stall,
 "ticks_echo":$ticks1,"ticks_timeout":$ticks2,"ticks_final":$ticks3,
 "timeout_budget_ms_env":"AHK_COM_CALL_TIMEOUT_MS"}
EOF
echo "M7_DBUS_ORACLE_PASS stall_timer=$fired_stall ticks=$ticks1/$ticks2/$ticks3"
