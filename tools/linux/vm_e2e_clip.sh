#!/bin/bash
# vm_e2e_clip.sh -- end-to-end: OnClipboardChange fires on wl-copy (Wayland/GNOME ext).
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
R=/tmp/xtest_e2e_clip.txt
exec > "$R" 2>&1
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
cat > /tmp/cb_test.ahk <<'EOF'
#Requires AutoHotkey v2.0
FileAppend("armed`n", "/tmp/cb_events.txt")
OnClipboardChange(ClipCb)
ClipCb(Type) {
    FileAppend("cb:" Type "`n", "/tmp/cb_events.txt")
}
Sleep 6000
ExitApp
EOF
rm -f /tmp/cb_events.txt
echo "--- run script (6s) ---"
"$AHK" /tmp/cb_test.ahk > /tmp/cb_run.log 2>&1 &
PID=$!
sleep 2
echo "--- wl-copy (should trigger cb:1) ---"
printf 'E2E_CLIP_TEST' | wl-copy 2>&1
echo "copy rc=$?"
sleep 1
echo "--- wl-paste clear (empty copy -> cb:0) ---"
printf '' | wl-copy 2>&1
sleep 1
wait "$PID"
echo "--- events file ---"
cat /tmp/cb_events.txt 2>/dev/null
echo "--- script stderr ---"
cat /tmp/cb_run.log 2>/dev/null | head -5
echo "e2e_clip_done=1"
