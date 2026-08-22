#!/bin/bash
# vm_tray_test.sh -- verify TrayTip -> Notifications on the GNOME session.
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
R=/tmp/xtest_tray.txt
exec > "$R" 2>&1
cat > /tmp/tt.ahk <<'EOF'
#Requires AutoHotkey v2.0
TrayTip("Hello from AHK", "Body text: TrayTip works")
TrayTip("Title only")
TrayTip()   ; empty: remove (no-op)
FileAppend("tt:done`n", "/tmp/tt_out.txt")
ExitApp
EOF
echo "=== TrayTip with daemon (capture Notify) ==="
dbus-monitor "type='method_call',interface='org.freedesktop.Notifications'" > /tmp/tt_mon.log 2>&1 &
MON=$!
sleep 1
rm -f /tmp/tt_out.txt
"$AHK" /tmp/tt.ahk 2>&1
sleep 2
kill "$MON" 2>/dev/null
cat /tmp/tt_out.txt 2>/dev/null
echo "--- Notify calls captured ---"
grep -c "member=Notify" /tmp/tt_mon.log 2>/dev/null
grep -A2 "member=Notify" /tmp/tt_mon.log 2>/dev/null | grep -E 'string|variant' | head -20
echo "=== TrayTip headless (no daemon) must not error ==="
cat > /tmp/tt2.ahk <<'EOF'
#Requires AutoHotkey v2.0
try {
    TrayTip("No daemon here", "x")
    FileAppend("tt2:noerr`n", "/tmp/tt2_out.txt")
} catch {
    FileAppend("tt2:err`n", "/tmp/tt2_out.txt")
}
ExitApp
EOF
rm -f /tmp/tt2_out.txt
env -u DISPLAY -u WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/nonexistent_tt_bus \
  "$AHK" /tmp/tt2.ahk 2>&1
cat /tmp/tt2_out.txt 2>/dev/null
echo "tray_done=1"
