#!/bin/bash
# M6 fault injection: add/remove/re-add independent uinput keyboards while one
# evdev-backend AHK process remains alive. Requires readable input and writable uinput.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
readable_event=0
for device in /dev/input/event*; do
  [ -r "$device" ] && { readable_event=1; break; }
done
if [ "$readable_event" != 1 ] || [ ! -w /dev/uinput ]; then
  echo EVDEV_HOTPLUG_SKIP input-read-and-uinput-write-required
  exit 2
fi
INJECTOR=/tmp/ahk-hotplug-uinput-inject
${CC:-cc} -O2 -Wall -Wextra "$ROOT/tools/linux/uinput-inject.c" -o "$INJECTOR" || exit 2
rm -f /tmp/ahk_hotplug_{ready,fired,combo,runtime.log} /tmp/ahk_hotplug_cmd_*
cat >/tmp/ahk_evdev_hotplug.ahk <<'EOF'
#Requires AutoHotkey v2.0
global hotplugCount := 0
sc030::{ ; physical KEY_B
    global hotplugCount
    hotplugCount += 1
    FileAppend(hotplugCount "`n", "/tmp/ahk_hotplug_fired")
}
a & b::{
    FileAppend("ghost-combo`n", "/tmp/ahk_hotplug_combo")
}
FileAppend("ready`n", "/tmp/ahk_hotplug_ready")
SetTimer(() => ExitApp(8), -60000)
EOF
APID=0
IPID=0
cleanup() {
  [ "$IPID" = 0 ] || kill "$IPID" 2>/dev/null || true
  [ "$APID" = 0 ] || kill "$APID" 2>/dev/null || true
  [ "$IPID" = 0 ] || wait "$IPID" 2>/dev/null || true
  [ "$APID" = 0 ] || wait "$APID" 2>/dev/null || true
  rm -f /tmp/ahk_hotplug_cmd_*
}
trap cleanup EXIT HUP INT TERM

AHK_INPUT_BACKEND=evdev "$BIN" /tmp/ahk_evdev_hotplug.ahk >/tmp/ahk_hotplug_runtime.log 2>&1 &
APID=$!
for _ in $(seq 1 200); do test -f /tmp/ahk_hotplug_ready && break; sleep .02; done
test -f /tmp/ahk_hotplug_ready || { echo EVDEV_HOTPLUG_RUNTIME_START_FAIL; exit 1; }
sleep 3

event_fd_count() {
  find "/proc/$APID/fd" -maxdepth 1 -type l -lname '/dev/input/event*' 2>/dev/null | wc -l
}
wait_count() {
  expected=$1
  loops=$2
  i=0
  while [ "$i" -lt "$loops" ]; do
    actual=$(event_fd_count)
    [ "$actual" = "$expected" ] && return 0
    sleep .05
    i=$((i + 1))
  done
  echo "EVDEV_HOTPLUG_FD_TIMEOUT expected=$expected actual=$(event_fd_count)" >&2
  return 1
}
wait_fire() {
  expected=$1
  loops=0
  while [ "$loops" -lt 200 ]; do
    actual=$(grep -c '^[0-9][0-9]*$' /tmp/ahk_hotplug_fired 2>/dev/null || true)
    [ -n "$actual" ] || actual=0
    [ "$actual" -ge "$expected" ] && return 0
    sleep .05
    loops=$((loops + 1))
  done
  return 1
}

baseline=$(event_fd_count)
counts=""
for cycle in 1 2 3; do
  cmd="/tmp/ahk_hotplug_cmd_$cycle"
  "$INJECTOR" "$cmd" >/tmp/ahk_hotplug_injector_$cycle.log 2>&1 &
  IPID=$!
  # udev/sysfs creates the event node asynchronously; runtime rescans every 2s.
  wait_count $((baseline + 1)) 140 \
    || { echo "EVDEV_HOTPLUG_ADD_FAIL cycle=$cycle"; cat /tmp/ahk_hotplug_runtime.log; exit 1; }
  counts="$counts+$((baseline + 1))"
  printf '48 tap\n' >"$cmd" # KEY_B -> sc030
  wait_fire "$cycle" \
    || { echo "EVDEV_HOTPLUG_FIRE_FAIL cycle=$cycle"; cat /tmp/ahk_hotplug_runtime.log; exit 1; }
  if [ "$cycle" -lt 3 ]; then
    # Remove the device with custom-combo prefix KEY_A still down. The next
    # keyboard's B must remain a plain sc030, not inherit a ghost A & B state.
    printf '30 down\n' >"$cmd"
    for _ in $(seq 1 40); do [ ! -e "$cmd" ] && break; sleep .02; done
    [ ! -e "$cmd" ] || { echo "EVDEV_HOTPLUG_PREFIX_INJECT_FAIL cycle=$cycle"; exit 1; }
    sleep .1
  fi
  kill -TERM "$IPID"
  wait "$IPID" || true
  IPID=0
  wait_count "$baseline" 100 \
    || { echo "EVDEV_HOTPLUG_REMOVE_FAIL cycle=$cycle"; cat /tmp/ahk_hotplug_runtime.log; exit 1; }
  counts="$counts-$baseline"
done

[ "$(tr '\n' ',' </tmp/ahk_hotplug_fired)" = '1,2,3,' ] && [ ! -e /tmp/ahk_hotplug_combo ] \
  || { echo EVDEV_HOTPLUG_COUNT_FAIL; cat /tmp/ahk_hotplug_fired /tmp/ahk_hotplug_combo; exit 1; }
cat >"$OUT/evdev-hotplug-summary.json" <<EOF
{"schema":1,"result":"pass","cycles":3,"fires":3,"baseline_event_fds":$baseline,"fd_transitions":"$counts","stale_fds":0,"held_prefix_removal_clean":true,"runtime_pid_stable":true}
EOF
echo "EVDEV_HOTPLUG_ORACLE_PASS cycles=3 fires=3 baseline_fds=$baseline stale_fds=0 ghost_prefix=0 runtime_pid_stable=1"
