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
rm -f /tmp/qt6_wayland.out /tmp/qt6-probe-click /tmp/qt6-probe-selection \
  /tmp/qt6-probe-value /tmp/qt6_wayland.dump
QT_QPA_PLATFORM=wayland QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
  "$OUT/qt6-probe" >/tmp/qt6_wayland_probe.log 2>&1 &
QPID=$!
sleep 5
cat >/tmp/qt6_wayland.ahk <<'EOF'
#Requires AutoHotkey v2.0
before := ControlGetText("QT6-ENTRY", "AHK Qt6 Probe")
partialTitle := ControlGetText("QT6-ENTRY", "Qt6 Probe")
A_ControlSendMode := "atspi"
ControlSendText("-追加", "QT6-ENTRY", "AHK Qt6 Probe")
after := ControlGetText("QT6-ENTRY", "AHK Qt6 Probe")
ControlClick("QT6-BUTTON", "AHK Qt6 Probe")
Sleep(400)
clicked := ControlGetText("QT6-ENTRY", "AHK Qt6 Probe")
items := ControlGetItems("QT6-LIST", "AHK Qt6 Probe")
findBravo := ControlFindItem("Bravo", "QT6-LIST", "AHK Qt6 Probe")
ControlChooseIndex(3, "QT6-LIST", "AHK Qt6 Probe")
choice3 := ControlGetChoice("QT6-LIST", "AHK Qt6 Probe")
index3 := ControlGetIndex("QT6-LIST", "AHK Qt6 Probe")
ControlChooseIndex(0, "QT6-LIST", "AHK Qt6 Probe")
index0 := ControlGetIndex("QT6-LIST", "AHK Qt6 Probe")
noChoice := 0
emptyChoiceError := 0
try ControlGetChoice("QT6-LIST", "AHK Qt6 Probe")
catch Error
{
    noChoice := 1
    emptyChoiceError := A_LastError
}
chooseBra := ControlChooseString("Bra", "QT6-LIST", "AHK Qt6 Probe")
choiceBra := ControlGetChoice("QT6-LIST", "AHK Qt6 Probe")
valueBefore := ControlGetText("QT6-SLIDER", "AHK Qt6 Probe")
ControlSetText("64", "QT6-SLIDER", "AHK Qt6 Probe")
valueAfter := ControlGetText("QT6-SLIDER", "AHK Qt6 Probe")
successError := A_LastError
invalidValue := 0
invalidValueError := 0
try ControlSetText("not-a-number", "QT6-SLIDER", "AHK Qt6 Probe")
catch ValueError
{
    invalidValue := 1
    invalidValueError := A_LastError
}
unsupportedSelection := 0
unsupportedSelectionError := 0
try ControlGetItems("QT6-ENTRY", "AHK Qt6 Probe")
catch OSError
{
    unsupportedSelection := 1
    unsupportedSelectionError := A_LastError
}
missingControl := 0
missingControlError := 0
try ControlGetItems("QT6-NOT-THERE", "AHK Qt6 Probe")
catch
{
    missingControl := 1
    missingControlError := A_LastError
}
Sleep(400)
FileAppend("before=" before "`npartialTitle=" partialTitle "`nafter=" after "`nclicked=" clicked
    "`nitems=" items.Length ":" items[1] ":" items[2] ":" items[3]
    "`nfind=" findBravo "`nchoice3=" choice3 "`nindex3=" index3
    "`nindex0=" index0 "`nnoChoice=" noChoice ":" emptyChoiceError
    "`nchooseBra=" chooseBra "`nchoiceBra=" choiceBra
    "`nvalue=" valueBefore ":" valueAfter ":" successError
    "`ninvalidValue=" invalidValue ":" invalidValueError
    "`nunsupportedSelection=" unsupportedSelection ":" unsupportedSelectionError
    "`nmissingControl=" missingControl ":" missingControlError "`n", "/tmp/qt6_wayland.out")
ExitApp
EOF
AHK_ATSPI_DUMP=/tmp/qt6_wayland.dump timeout -k 2 30 "$BIN" /tmp/qt6_wayland.ahk \
  >/tmp/qt6_wayland_ahk.log 2>&1
rc=$?
cat >/tmp/qt6_pending.ahk <<'EOF'
#Requires AutoHotkey v2.0
global timerFired := 0, timerNestedError := -1, timerNestedText := "sentinel"
TimerProbe() {
    global timerFired, timerNestedError, timerNestedText
    timerFired := 1
    timerNestedText := ControlGetText("QT6-ENTRY", "AHK Qt6 Probe")
    timerNestedError := A_LastError
    FileAppend("timer nested=" timerNestedError "`n", "/tmp/qt6_pending_timer.out")
}
SetTimer(TimerProbe, -50)
started := A_TickCount
text := ControlGetText("QT6-ENTRY", "AHK Qt6 Probe")
elapsed := A_TickCount - started
FileAppend("fired=" timerFired " elapsed=" elapsed " text=" text
    " nestedError=" timerNestedError " nestedText=" timerNestedText " outerError=" A_LastError "`n", "/tmp/qt6_pending.out")
ExitApp
EOF
rm -f /tmp/qt6_pending.out /tmp/qt6_pending_timer.out /tmp/qt6_pending.dump
AHK_ATSPI_TEST_REPLY_DELAY_MS=200 AHK_ATSPI_DUMP=/tmp/qt6_pending.dump \
  timeout -k 2 30 "$BIN" /tmp/qt6_pending.ahk >/tmp/qt6_pending.log 2>&1
pending_rc=$?
cat >/tmp/qt6_budget_error.ahk <<'EOF'
#Requires AutoHotkey v2.0
caught := 0, code := 0
try ControlGetItems("QT6-NOT-THERE", "AHK Qt6 Probe")
catch
{
    caught := 1
    code := A_LastError
}
FileAppend("caught=" caught " code=" code "`n", "/tmp/qt6_budget_error.out")
ExitApp
EOF
rm -f /tmp/qt6_budget_error.out
AHK_ATSPI_TOTAL_BUDGET_MS=1 timeout -k 2 20 "$BIN" /tmp/qt6_budget_error.ahk \
  >/tmp/qt6_budget_error.log 2>&1
budget_rc=$?
kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
[ "$rc" = 0 ] || { echo QT6_WAYLAND_AHK_FAIL; cat /tmp/qt6_wayland_ahk.log; exit 1; }
pending_elapsed="$(sed -n 's/.*elapsed=\([0-9][0-9]*\).*/\1/p' /tmp/qt6_pending.out | head -1)"
pending_calls="$(head -1 /tmp/qt6_pending.dump | sed -n 's/.*pending_calls=\([0-9][0-9]*\).*/\1/p')"
pump_slices="$(head -1 /tmp/qt6_pending.dump | sed -n 's/.*pump_slices=\([0-9][0-9]*\).*/\1/p')"
[ "$pending_rc" = 0 ] && grep -q '^fired=1 .*nestedError=16 nestedText= outerError=0$' /tmp/qt6_pending.out \
  && grep -q '^timer nested=16$' /tmp/qt6_pending_timer.out \
  && [ -n "$pending_elapsed" ] && [ "$pending_elapsed" -ge 180 ] && [ "$pending_elapsed" -lt 3000 ] \
  && [ -n "$pending_calls" ] && [ "$pending_calls" -gt 0 ] \
  && [ -n "$pump_slices" ] && [ "$pump_slices" -gt 0 ] \
  || { echo QT6_PENDING_MAINLOOP_FAIL; cat /tmp/qt6_pending.out /tmp/qt6_pending.log; head -1 /tmp/qt6_pending.dump; exit 1; }
[ "$budget_rc" = 0 ] && grep -q '^caught=1 code=110$' /tmp/qt6_budget_error.out \
  || { echo QT6_BUDGET_LASTERROR_FAIL; cat /tmp/qt6_budget_error.out /tmp/qt6_budget_error.log; exit 1; }
grep -q '^before=你好-Qt6$' /tmp/qt6_wayland.out \
  && grep -q '^partialTitle=你好-Qt6$' /tmp/qt6_wayland.out \
  || { echo QT6_TEXT_READ_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^after=你好-Qt6-追加$' /tmp/qt6_wayland.out \
  || { echo QT6_TEXT_WRITE_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^clicked=clicked-Qt6$' /tmp/qt6_wayland.out \
  || { echo QT6_ACTION_TEXT_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^clicked$' /tmp/qt6-probe-click \
  || { echo QT6_ACTION_MARKER_FAIL; cat /tmp/qt6_wayland_probe.log; exit 1; }
grep -q '^items=3:Alpha:Bravo:世界$' /tmp/qt6_wayland.out \
  || { echo QT6_SELECTION_ITEMS_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^find=2$' /tmp/qt6_wayland.out \
  || { echo QT6_SELECTION_FIND_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^choice3=世界$' /tmp/qt6_wayland.out \
  || { echo QT6_SELECTION_CHOICE_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^index3=3$' /tmp/qt6_wayland.out \
  || { echo QT6_SELECTION_INDEX_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^index0=0$' /tmp/qt6_wayland.out \
  || { echo QT6_SELECTION_CLEAR_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^noChoice=1:61$' /tmp/qt6_wayland.out \
  || { echo QT6_SELECTION_EMPTY_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^chooseBra=2$' /tmp/qt6_wayland.out \
  || { echo QT6_SELECTION_STRING_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^choiceBra=Bravo$' /tmp/qt6_wayland.out \
  || { echo QT6_SELECTION_FINAL_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^Bravo$' /tmp/qt6-probe-selection \
  || { echo QT6_SELECTION_MARKER_FAIL; cat /tmp/qt6-probe-selection; exit 1; }
grep -q '^value=25:64:0$' /tmp/qt6_wayland.out \
  || { echo QT6_VALUE_API_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^64$' /tmp/qt6-probe-value \
  || { echo QT6_VALUE_MARKER_FAIL; cat /tmp/qt6-probe-value; exit 1; }
grep -q '^invalidValue=1:22$' /tmp/qt6_wayland.out \
  || { echo QT6_VALUE_ERROR_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^unsupportedSelection=1:95$' /tmp/qt6_wayland.out \
  || { echo QT6_SELECTION_UNSUPPORTED_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
grep -q '^missingControl=1:2$' /tmp/qt6_wayland.out \
  || { echo QT6_MISSING_CONTROL_ERROR_FAIL; cat /tmp/qt6_wayland.out; exit 1; }
cat >/tmp/qt6_bus_error.ahk <<'EOF'
#Requires AutoHotkey v2.0
caught := 0, code := 0
try ControlGetItems("ANY", "ANY")
catch OSError
{
    caught := 1
    code := A_LastError
}
FileAppend("caught=" caught " code=" code "`n", "/tmp/qt6_bus_error.out")
ExitApp
EOF
rm -f /tmp/qt6_bus_error.out /tmp/ahk-no-session-bus
DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/ahk-no-session-bus \
  XDG_SESSION_TYPE=wayland timeout -k 2 20 "$BIN" /tmp/qt6_bus_error.ahk \
  >/tmp/qt6_bus_error.log 2>&1
bus_rc=$?
[ "$bus_rc" = 0 ] && grep -q '^caught=1 code=107$' /tmp/qt6_bus_error.out \
  || { echo QT6_BUS_LASTERROR_FAIL; cat /tmp/qt6_bus_error.out /tmp/qt6_bus_error.log; exit 1; }
header="$(head -1 /tmp/qt6_wayland.dump 2>/dev/null)"
cat >"$OUT/qt6-capture-summary.json" <<EOF
{"schema":1,"result":"pass","qt_version":"$(pkg-config --modversion Qt6Widgets)","x11_window_count":1,"wayland_before":"你好-Qt6","wayland_after":"你好-Qt6-追加","action_text":"clicked-Qt6","selection_items":["Alpha","Bravo","世界"],"selection_final":"Bravo","value_before":25,"value_after":64,"last_error":{"success":0,"enoent":2,"einval":22,"enodata":61,"enotsup":95,"enotconn":107,"etimedout":110},"pending":{"timer_fired":true,"elapsed_ms":$pending_elapsed,"calls":$pending_calls,"pump_slices":$pump_slices,"nested_errno":16,"outer_errno":0},"atspi_header":"$header"}
EOF
echo "QT6_CAPTURE_ORACLE_PASS version=$(pkg-config --modversion Qt6Widgets) x11=1 atspi=text+action+selection+value last_error=0,2,22,61,95,107,110 pending_calls=$pending_calls pump_slices=$pump_slices timer=1 nested=EBUSY"
