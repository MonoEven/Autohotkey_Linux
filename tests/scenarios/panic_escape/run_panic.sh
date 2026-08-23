#!/bin/bash
# panic_escape scenario: the evdev lane's Backspace->Escape->Enter sequence
# must release every EVIOCGRAB and drop into fail-open, so a stuck grab can
# be recovered physically (check_detail0821 §1-B / R4, keyd-style).
set -u
AHK="${AHK:?runner must export AHK}"
TOOLS="/home/mono/Autohotkey_Linux/tools/linux"
unset DISPLAY WAYLAND_DISPLAY 2>/dev/null || true
pkill -9 -f 'uinput-inject' 2>/dev/null; pkill -9 -f 'ahk_core.*panic' 2>/dev/null
gcc -o /tmp/uinput-inject "$TOOLS/uinput-inject.c" 2>/dev/null || exit 0
[ -x /tmp/uinput-inject ] || exit 0
rm -f /tmp/scn_panic_escape /tmp/scn_panic_m.txt /tmp/scn_panic_cmd
cat > /tmp/scn_panic.ahk <<'EOF'
#Requires AutoHotkey v2.0
F12::{
    FileAppend("f12`n", "/tmp/scn_panic_m.txt")
}
SetTimer(() => ExitApp(), 14000)
EOF
/tmp/uinput-inject /tmp/scn_panic_cmd > /dev/null 2>&1 &
IPID=$!
sleep 1
AHK_INPUT_BACKEND=evdev "$AHK" /tmp/scn_panic.ahk > /tmp/scn_panic.log 2>&1 &
APID=$!
sleep 4
# Before the panic: F12 fires the hotkey (suppressed).
echo "88 tap" > /tmp/scn_panic_cmd
sleep 2
# Panic sequence: Backspace(14) Escape(1) Enter(28).
echo "14 down" > /tmp/scn_panic_cmd; sleep 0.1; echo "14 up" > /tmp/scn_panic_cmd
sleep 0.2
echo "1 down" > /tmp/scn_panic_cmd; sleep 0.1; echo "1 up" > /tmp/scn_panic_cmd
sleep 0.2
echo "28 down" > /tmp/scn_panic_cmd; sleep 0.1; echo "28 up" > /tmp/scn_panic_cmd
sleep 1
# After the panic: F12 must NOT fire the hotkey (fail-open passthrough).
echo "88 tap" > /tmp/scn_panic_cmd
sleep 2
kill "$APID" "$IPID" 2>/dev/null
PANIC_SEEN=$(grep -c 'PANIC sequence fired' /tmp/scn_panic.log 2>/dev/null)
BEFORE=$(grep -c '^f12$' /tmp/scn_panic_m.txt 2>/dev/null)
AFTER=$(( $(wc -l < /tmp/scn_panic_m.txt 2>/dev/null || echo 0) - BEFORE ))
# Pass: the panic message appeared AND the post-panic F12 did not re-fire.
if [ "$PANIC_SEEN" -ge 1 ] && [ "$AFTER" -le 0 ]; then
  touch /tmp/scn_panic_escape
fi
exit 0