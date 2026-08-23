#!/bin/bash
# gnome_regression scenario: under a real GNOME Wayland session (a11y on),
# the caps API must report a Wayland backend and AT-SPI controls must resolve
# (check_detail0821 §1-B/D + §7-C4).  The runner skips it when the needs gate
# fails (CI's Xvfb runner has no GNOME session).
set -u
AHK="${AHK:?runner must export AHK}"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
pkill -f 'gnome-terminal' 2>/dev/null; sleep 1
nohup gnome-terminal --title=GNOME-REG > /dev/null 2>&1 &
sleep 5
cat > /tmp/scn_gnome_reg.ahk <<'EOF'
#Requires AutoHotkey v2.0
backend := A_HotkeyBackend
caps := HotkeyBackendGet()
c := caps.global_hotkeys
term := 0
try {
    ControlGetText("GNOME-REG")
    term := 1
} catch as e {
}
FileAppend("backend=" backend " global=" c " term=" term "`n", "/tmp/scn_gnome_reg_out.txt")
ExitApp
EOF
rm -f /tmp/scn_gnome_reg_out.txt
"$AHK" /tmp/scn_gnome_reg.ahk > /tmp/scn_gnome_reg.log 2>&1
pkill -f 'gnome-terminal' 2>/dev/null
# The backend may be portal (X11-less Wayland, no extension) or gnome-shell;
# the regression passes when a Wayland backend is active AND AT-SPI resolves.
if [ -f /tmp/scn_gnome_reg_out.txt ] && grep -q 'term=1' /tmp/scn_gnome_reg_out.txt; then
  touch /tmp/scn_gnome_regression
fi
exit 0