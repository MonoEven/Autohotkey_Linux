#!/bin/bash
# vm_ext_signal4.sh -- ClipboardChanged via wl-copy (Wayland-native set).
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
R=/tmp/xtest_ext_signal4.txt
exec > "$R" 2>&1
echo "--- ext state ---"
timeout 15 gnome-extensions show ahk-global-hotkeys@autohotkey.org 2>&1 | grep -iE 'State' | head -1
echo "--- dbus-monitor ClipboardChanged + wl-copy ---"
dbus-monitor "type='signal',interface='io.github.autohotkey.GlobalHotkeys1',member='ClipboardChanged'" > /tmp/cb_sig4.log 2>&1 &
MON=$!
sleep 1
printf 'WL_COPY_SIGNAL_TEST' | wl-copy 2>&1
echo "wl-copy rc=$?"
sleep 1
printf 'WL_COPY_SIGNAL_TEST_2' | wl-copy 2>&1
sleep 2
echo "--- wl-paste readback ---"
wl-paste 2>&1
kill "$MON" 2>/dev/null
echo "--- captured ClipboardChanged count ---"
grep -c "member=ClipboardChanged" /tmp/cb_sig4.log 2>/dev/null || echo 0
grep -A2 "member=ClipboardChanged" /tmp/cb_sig4.log 2>/dev/null | grep -E 'uint32' | head -6
echo "ext_signal4_done=1"
