#!/bin/bash
# evdev_remap scenario: the evdev/uinput lane reads a physical key, fires the
# remap hotkey, and replays the mapped key through uinput.  Needs a writable
# /dev/uinput (the runner skips when the needs gate fails).
set -u
AHK="${AHK:?runner must export AHK}"
TOOLS="/home/mono/Autohotkey_Linux/tools/linux"
pkill -9 -f 'uinput-inject' 2>/dev/null; pkill -9 -f 'ahk_core.*evrem' 2>/dev/null
gcc -o /tmp/uinput-inject "$TOOLS/uinput-inject.c" 2>/dev/null || exit 0
[ -x /tmp/uinput-inject ] || exit 0
rm -f /tmp/scn_evdev_remap /tmp/scn_ev_marker /tmp/scn_uinj_cmd
cat > /tmp/scn_evrem.ahk <<'EOF'
#Requires AutoHotkey v2.0
a::{
    FileAppend("fired`n", "/tmp/scn_ev_marker")
    Send("b")
}
SetTimer(() => ExitApp(), 12000)
EOF
/tmp/uinput-inject /tmp/scn_uinj_cmd > /dev/null 2>&1 &
IPID=$!
sleep 1
AHK_INPUT_BACKEND=evdev "$AHK" /tmp/scn_evrem.ahk > /tmp/scn_evrem.log 2>&1 &
APID=$!
sleep 4
echo "30 tap" > /tmp/scn_uinj_cmd   # KEY_A
sleep 3
kill "$APID" "$IPID" 2>/dev/null
if [ -f /tmp/scn_ev_marker ]; then
  touch /tmp/scn_evdev_remap
fi
exit 0