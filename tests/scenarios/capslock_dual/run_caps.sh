#!/bin/bash
# capslock_dual scenario: CapsLock tap -> CapsLock, hold -> Esc via the
# evdev/uinput lane (check_detail0821 §1-B).  Verifies the hold/tap decision
# from the hotkey markers AND the uinput replay of Esc through evtest.
set -u
AHK="${AHK:?runner must export AHK}"
TOOLS="/home/mono/Autohotkey_Linux/tools/linux"
# The evdev/uinput lane is a pure kernel path: no X/Wayland.  Unset them so
# the runner's Xvfb display cannot cause an X connection attempt (XIO).
unset DISPLAY WAYLAND_DISPLAY 2>/dev/null || true
pkill -9 -f 'uinput-inject' 2>/dev/null; pkill -9 -f 'ahk_core.*capsc' 2>/dev/null; pkill -9 -f evtest 2>/dev/null
gcc -o /tmp/uinput-inject "$TOOLS/uinput-inject.c" 2>/dev/null || exit 0
[ -x /tmp/uinput-inject ] || exit 0
rm -f /tmp/scn_capslock_dual /tmp/scn_caps_m.txt /tmp/scn_caps_cmd
cat > /tmp/scn_capsc.ahk <<'EOF'
#Requires AutoHotkey v2.0
down := 0
CapsLock::{
    global down
    down := A_TickCount
}
CapsLock up::{
    global down
    if (A_TickCount - down < 200)
        Send("{CapsLock}")
    else
        Send("{Esc}")
    FileAppend("u:" (A_TickCount - down) "`n", "/tmp/scn_caps_m.txt")
}
SetTimer(() => ExitApp(), 15000)
EOF
/tmp/uinput-inject /tmp/scn_caps_cmd > /dev/null 2>&1 &
IPID=$!
sleep 1
AHK_INPUT_BACKEND=evdev "$AHK" /tmp/scn_capsc.ahk > /tmp/scn_capsc.log 2>&1 &
APID=$!
sleep 4
# A hold injection first: creates the uinput device via Send("{Esc}").
echo "58 down" > /tmp/scn_caps_cmd; sleep 0.5; echo "58 up" > /tmp/scn_caps_cmd
sleep 2.5
# Grab the AHK uinput device to watch the Esc replay of the next hold.
DEV=""
for n in /sys/class/input/event*/device/name; do
  [ -f "$n" ] || continue
  if grep -qi 'AHK' "$n" 2>/dev/null; then
    DEV="$(basename "$(dirname "$(dirname "$n")")")"
    break
  fi
done
ESC_SEEN=0
if [ -n "$DEV" ]; then
  timeout 15 evtest --grab "/dev/input/$DEV" > /tmp/scn_caps_ev.log 2>&1 &
  EPID=$!
  sleep 1.5
  echo "58 down" > /tmp/scn_caps_cmd; sleep 0.6; echo "58 up" > /tmp/scn_caps_cmd
  sleep 3.5
  kill "$EPID" 2>/dev/null
  grep -q 'KEY_ESC' /tmp/scn_caps_ev.log 2>/dev/null && ESC_SEEN=1
fi
kill "$APID" "$IPID" 2>/dev/null
# Decision check: a hold must have been judged >= 200ms (Esc path).
HOLD_OK=$(grep -c '^u:[2-9][0-9][0-9]' /tmp/scn_caps_m.txt 2>/dev/null)
if [ "$HOLD_OK" -ge 1 ] && [ "$ESC_SEEN" = "1" ]; then
  touch /tmp/scn_capslock_dual
fi
exit 0