#!/bin/bash
# GUI-1 Qt6: prove a real Qt window is captured through X11 window enumeration
# and a real Wayland Qt control is read, edited and activated through AT-SPI.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
command -v pkg-config >/dev/null || exit 2
pkg-config --exists Qt6Widgets || { echo QT6_CAPTURE_SKIP Qt6Widgets-missing; exit 2; }
${CXX:-g++} -std=c++17 -O2 -fPIC -o "$OUT/qt6-probe" \
  "$ROOT/tests/oracle/qt6_probe.cpp" $(pkg-config --cflags --libs Qt6Widgets) \
  || exit 2

# X11/EWMH-fallback lane under Xvfb.
cat >/tmp/qt6_x11.ahk <<'EOF'
#Requires AutoHotkey v2.0
count := WinGetList("AHK Qt6 Probe").Length
FileAppend("count=" count "`n", "/tmp/qt6_x11.out")
ExitApp
EOF
rm -f /tmp/qt6_x11.out
xvfb-run -a bash -c "export QT_QPA_PLATFORM=xcb; \
  '$OUT/qt6-probe' >/tmp/qt6_x11_probe.log 2>&1 & QPID=\$!; sleep 2; \
  '$BIN' /tmp/qt6_x11.ahk >/tmp/qt6_x11_ahk.log 2>&1; \
  kill \$QPID 2>/dev/null; wait \$QPID 2>/dev/null"
[ "$(cat /tmp/qt6_x11.out 2>/dev/null)" = "count=1" ] \
  || { echo QT6_X11_CAPTURE_FAIL; cat /tmp/qt6_x11.out /tmp/qt6_x11_ahk.log 2>/dev/null; exit 1; }

# Wayland/AT-SPI lane in the real GNOME session.
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
rm -f /tmp/qt6_wayland.out /tmp/qt6-probe-click /tmp/qt6_wayland.dump
QT_QPA_PLATFORM=wayland QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
  "$OUT/qt6-probe" >/tmp/qt6_wayland_probe.log 2>&1 &
QPID=$!
sleep 5
cat >/tmp/qt6_wayland.ahk <<'EOF'
#Requires AutoHotkey v2.0
before := ControlGetText("QT6-ENTRY", "AHK Qt6 Probe")
A_ControlSendMode := "atspi"
ControlSendText("-追加", "QT6-ENTRY", "AHK Qt6 Probe")
after := ControlGetText("QT6-ENTRY", "AHK Qt6 Probe")
ControlClick("QT6-BUTTON", "AHK Qt6 Probe")
Sleep(400)
clicked := ControlGetText("QT6-ENTRY", "AHK Qt6 Probe")
FileAppend("before=" before "`nafter=" after "`nclicked=" clicked "`n", "/tmp/qt6_wayland.out")
ExitApp
EOF
AHK_ATSPI_DUMP=/tmp/qt6_wayland.dump timeout -k 2 30 "$BIN" /tmp/qt6_wayland.ahk \
  >/tmp/qt6_wayland_ahk.log 2>&1
rc=$?
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
[ "$rc" = 0 ] || { echo QT6_WAYLAND_AHK_FAIL; cat /tmp/qt6_wayland_ahk.log; exit 1; }
grep -q '^before=你好-Qt6$' /tmp/qt6_wayland.out \
  || { echo QT6_TEXT_READ_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^after=你好-Qt6-追加$' /tmp/qt6_wayland.out \
  || { echo QT6_TEXT_WRITE_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^clicked=clicked-Qt6$' /tmp/qt6_wayland.out \
  || { echo QT6_ACTION_TEXT_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^clicked$' /tmp/qt6-probe-click \
  || { echo QT6_ACTION_MARKER_FAIL; cat /tmp/qt6_wayland_probe.log; exit 1; }
header="$(head -1 /tmp/qt6_wayland.dump 2>/dev/null)"
cat >"$OUT/qt6-capture-summary.json" <<EOF
{"schema":1,"result":"pass","qt_version":"$(pkg-config --modversion Qt6Widgets)","x11_window_count":1,"wayland_before":"你好-Qt6","wayland_after":"你好-Qt6-追加","action_text":"clicked-Qt6","atspi_header":"$header"}
EOF
echo "QT6_CAPTURE_ORACLE_PASS version=$(pkg-config --modversion Qt6Widgets) x11=1 atspi=read+write+action"
