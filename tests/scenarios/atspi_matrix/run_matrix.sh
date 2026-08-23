#!/bin/bash
# atspi_matrix scenario: under a GNOME Wayland session with a11y enabled,
# ControlGetText must find gnome-terminal + firefox controls via AT-SPI
# (check_detail0821 §7-C4).  The runner skips it when the needs gate fails.
set -u
AHK="${AHK:?runner must export AHK}"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
pkill -f 'gnome-terminal' 2>/dev/null; pkill -f 'firefox' 2>/dev/null
sleep 1
nohup gnome-terminal --title=MATRIX-TERM > /dev/null 2>&1 &
sleep 4
nohup firefox --new-window about:blank > /dev/null 2>&1 &
sleep 10
cat > /tmp/scn_matrix.ahk <<'EOF'
#Requires AutoHotkey v2.0
term_ok := 0
ff_ok := 0
try {
    ControlGetText("MATRIX-TERM")
    term_ok := 1
} catch as e {
}
try {
    ControlGetText("New Tab")
    ff_ok := 1
} catch as e {
}
FileAppend("term=" term_ok " ff=" ff_ok "`n", "/tmp/scn_matrix_out.txt")
ExitApp
EOF
rm -f /tmp/scn_matrix_out.txt
AHK_ATSPI_DUMP=/tmp/scn_matrix_dump.txt "$AHK" /tmp/scn_matrix.ahk > /dev/null 2>&1
pkill -f 'gnome-terminal' 2>/dev/null; pkill -f 'firefox' 2>/dev/null
if [ -f /tmp/scn_matrix_out.txt ] && grep -q 'term=1 ff=1' /tmp/scn_matrix_out.txt; then
  touch /tmp/scn_atspi_matrix
fi
exit 0