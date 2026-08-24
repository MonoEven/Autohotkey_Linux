#!/bin/bash
# GNOME Wayland AT-SPI matrix: terminal + Firefox discovery and an independent
# GTK3 Entry whose Unicode text is read through ControlGetText.
set -u
AHK="${AHK:?runner must export AHK}"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
pkill -f 'gnome-terminal' 2>/dev/null; pkill -f 'firefox' 2>/dev/null
pkill -f 'gtk_utf8_probe.py' 2>/dev/null
sleep 1
nohup gnome-terminal --title=MATRIX-TERM > /dev/null 2>&1 &
sleep 4
nohup firefox --new-window about:blank > /dev/null 2>&1 &
nohup env GDK_BACKEND=wayland python3 "$HERE/gtk_utf8_probe.py" > /tmp/scn_gtk_utf8.log 2>&1 &
sleep 10
cat > /tmp/scn_matrix.ahk <<'EOF'
#Requires AutoHotkey v2.0
term_ok := 0
ff_ok := 0
utf8_ok := 0
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
try {
    utf8_ok := ControlGetText("AHK-UTF8-ENTRY") = "你好-AT-SPI" ? 1 : 0
} catch as e {
}
FileAppend("term=" term_ok " ff=" ff_ok " utf8=" utf8_ok "`n", "/tmp/scn_matrix_out.txt")
ExitApp
EOF
rm -f /tmp/scn_matrix_out.txt
AHK_ATSPI_DUMP=/tmp/scn_matrix_dump.txt "$AHK" /tmp/scn_matrix.ahk > /tmp/scn_matrix_ahk.log 2>&1
pkill -f 'gnome-terminal' 2>/dev/null; pkill -f 'firefox' 2>/dev/null
pkill -f 'gtk_utf8_probe.py' 2>/dev/null
if [ -f /tmp/scn_matrix_out.txt ] \
   && grep -q 'term=1 ff=1 utf8=1' /tmp/scn_matrix_out.txt \
   && grep -Eq '^# cache_apps=[1-9][0-9]* .*cache_items=[1-9][0-9]* .*pending_calls=[1-9][0-9]* .*pump_slices=[1-9][0-9]*' /tmp/scn_matrix_dump.txt; then
  touch /tmp/scn_atspi_matrix
fi
exit 0
