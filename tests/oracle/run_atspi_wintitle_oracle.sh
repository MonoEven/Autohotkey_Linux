#!/bin/bash
# M5-C oracle: Control* WinTitle limiting over AT-SPI.  Two independent GTK
# processes expose an entry whose accessible NAME is "OK" but with different
# text; ControlGetText("OK", "WintA") must resolve inside App A only.
# Requires a GNOME (Wayland) session with accessibility on.
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

pkill -f 'gtk-ok --title' 2>/dev/null
sleep .5
rm -f /tmp/m5c.out /tmp/m5c_ahk.log
"$OUT/gtk-ok" --title "WintA" --name "OK" --text "HelloA" >/tmp/atspi_a.log 2>&1 &
APID=$!
"$OUT/gtk-ok" --title "WintB" --name "OK" --text "HelloB" >/tmp/atspi_b.log 2>&1 &
BPID=$!
sleep 4

cat >/tmp/m5c.ahk <<'EOF'
#Requires AutoHotkey v2.0
a := ControlGetText("OK", "WintA")
b := ControlGetText("OK", "WintB")
c := ControlGetText("Nope", "WintA")  ; absent control -> empty, no cross-match
FileAppend("a=" a " b=" b " c=" c "`n", "/tmp/m5c.out")
ExitApp
EOF
"$BIN" /tmp/m5c.ahk >/tmp/m5c_ahk.log 2>&1
rc=$?
RESULT="$(cat /tmp/m5c.out 2>/dev/null)"
kill "$APID" "$BPID" 2>/dev/null
wait "$APID" 2>/dev/null; wait "$BPID" 2>/dev/null
pkill -f 'gtk-ok --title' 2>/dev/null

[ "$rc" = 0 ] || { echo "AHK_RC_FAIL rc=$rc"; cat /tmp/m5c_ahk.log; exit 1; }
[ "$RESULT" = "a=HelloA b=HelloB c=" ] || { echo "WINITLE_LIMIT_FAIL result=[$RESULT]"; cat /tmp/m5c_ahk.log /tmp/atspi_a.log /tmp/atspi_b.log 2>/dev/null; exit 1; }
cat >"$OUT/atspi-wintitle-summary.json" <<EOF
{"schema":1,"result":"pass","wintitle_limiting":true,"a":"HelloA","b":"HelloB","absent_control":"empty"}
EOF
echo "ATSPI_WINITLE_ORACLE_PASS a=HelloA b=HelloB c="
