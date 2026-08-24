#!/bin/bash
# M5-C: A_ControlSendMode behavior on a real GNOME Wayland accessibility tree.
# atspi mode appends Unicode through EditableText without an X display; focus
# mode and complex Send syntax fail explicitly instead of pretending success.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
CC="${CC:-cc}"
$CC -O2 -Wall -Wextra -o "$OUT/gtk-ok" "$ROOT/tests/oracle/gtk_ok.c" \
  $(pkg-config --cflags --libs gtk+-3.0) || exit 2

export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY

pkill -f 'gtk-ok --title SendModeWin' 2>/dev/null
rm -f /tmp/m5sendmode.out /tmp/m5sendmode.log
"$OUT/gtk-ok" --title SendModeWin --name SEND-ENTRY --text Base \
  >/tmp/m5sendmode_gtk.log 2>&1 &
GPID=$!
sleep 4
cat >/tmp/m5sendmode.ahk <<'EOF'
#Requires AutoHotkey v2.0
out := "/tmp/m5sendmode.out"
FileAppend("default=" A_ControlSendMode "`n", out)
A_ControlSendMode := "atspi"
ControlSendText("-世界", "SEND-ENTRY", "SendModeWin")
text := ControlGetText("SEND-ENTRY", "SendModeWin")
complex := 0
try ControlSend("^a", "SEND-ENTRY", "SendModeWin")
catch OSError
    complex := 1
invalid := 0
try A_ControlSendMode := "bogus"
catch ValueError
    invalid := 1
A_ControlSendMode := "focus"
focus_error := 0
try ControlSendText("x", "SEND-ENTRY", "SendModeWin")
catch TargetError
    focus_error := 1
FileAppend("text=" text " complex=" complex " invalid=" invalid
    " focus_error=" focus_error " restore=" A_ControlSendMode "`n", out)
ExitApp
EOF
AHK_ATSPI_DUMP=/tmp/m5sendmode.dump timeout -k 2 30 "$BIN" /tmp/m5sendmode.ahk \
  >/tmp/m5sendmode.log 2>&1
rc=$?
kill "$GPID" 2>/dev/null; wait "$GPID" 2>/dev/null
pkill -f 'gtk-ok --title SendModeWin' 2>/dev/null
RESULT="$(cat /tmp/m5sendmode.out 2>/dev/null)"
[ "$rc" = 0 ] || { echo "CONTROLSEND_MODE_RC_FAIL rc=$rc"; cat /tmp/m5sendmode.log; exit 1; }
printf '%s\n' "$RESULT" | grep -q '^default=focus$' \
  || { echo "CONTROLSEND_DEFAULT_FAIL result=[$RESULT]"; exit 1; }
printf '%s\n' "$RESULT" | grep -q '^text=Base-世界 complex=1 invalid=1 focus_error=1 restore=focus$' \
  || { echo "CONTROLSEND_ATSPI_FAIL result=[$RESULT]"; cat /tmp/m5sendmode.log; exit 1; }
cat >"$OUT/controlsend-mode-summary.json" <<EOF
{"schema":1,"result":"pass","default":"focus","atspi_text":"Base-世界","complex_not_supported":true,"invalid_mode_rejected":true,"focus_without_x_rejected":true}
EOF
echo "CONTROLSEND_MODE_ORACLE_PASS text=Base-世界"
