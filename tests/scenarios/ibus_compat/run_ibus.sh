#!/bin/bash
# ibus_compat scenario: under an IBus session the port must run and
# SendText("中文测试") must complete without crashing (the native-Wayland
# clipboard fallback injects Ctrl+V through uinput).  check_detail0821 §6-U4.
set -u
AHK="${AHK:?runner must export AHK}"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
cat > /tmp/scn_ibus.ahk <<'EOF'
#Requires AutoHotkey v2.0
SendText("中文测试")
FileAppend("sent`n", "/tmp/scn_ibus_out.txt")
ExitApp
EOF
rm -f /tmp/scn_ibus_out.txt
"$AHK" /tmp/scn_ibus.ahk > /tmp/scn_ibus.log 2>&1
if [ -f /tmp/scn_ibus_out.txt ] && grep -q 'sent' /tmp/scn_ibus_out.txt; then
  touch /tmp/scn_ibus_compat
fi
exit 0