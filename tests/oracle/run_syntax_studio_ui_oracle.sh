#!/bin/bash
# Syntax Studio UI integration oracle (Audit47 I13).
# This is an actual X11 GUI interaction test; the source guard alone is not
# considered proof of list selection, filtering, check, or run behavior.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
EXAMPLE="$ROOT/examples/gui/syntax_studio.ahk"
OUT="$ROOT/tests/oracle/out"
TRACE=/tmp/ahk-syntax-studio-trace
mkdir -p "$OUT"
command -v Xvfb >/dev/null || { echo SYNTAX_STUDIO_SKIP xvfb; exit 2; }
command -v xdotool >/dev/null || { echo SYNTAX_STUDIO_SKIP xdotool; exit 2; }
test -x "$BIN" || { echo SYNTAX_STUDIO_FAIL binary; exit 1; }
rm -f "$TRACE" /tmp/ahk-studio-exercise.txt "$OUT/syntax-studio-ui-summary.json"
XDISPLAY=:17
Xvfb "$XDISPLAY" -screen 0 1280x800x24 > /tmp/syntax-studio-xvfb.log 2>&1 &
XVPID=$!
APID=0
cleanup() {
  [ "$APID" = 0 ] || kill -9 "$APID" 2>/dev/null || true
  kill "$XVPID" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
sleep 2
DISPLAY="$XDISPLAY" GDK_BACKEND=x11 XDG_SESSION_TYPE=x11 env -u WAYLAND_DISPLAY \
  AHK_SYNTAX_STUDIO_TRACE="$TRACE" "$BIN" "$EXAMPLE" > /tmp/syntax-studio-ahk.log 2>&1 &
APID=$!
WIN=""
for _ in $(seq 1 100); do
  WIN=$(DISPLAY="$XDISPLAY" xdotool search --onlyvisible --name 'AHK v2 Syntax Studio' 2>/dev/null | tail -1 || true)
  [ -n "$WIN" ] && break
  sleep .1
done
[ -n "$WIN" ] || { echo SYNTAX_STUDIO_FAIL window; cat /tmp/syntax-studio-ahk.log; exit 1; }
# Initial list is at x=120; Conditions row is y=212 in the fixed 1220x770 layout.
DISPLAY="$XDISPLAY" xdotool mousemove --sync 120 212 click 1 >/dev/null 2>&1 || exit 1
sleep .3
# Check button, then Run button, both use the selected Conditions exercise.
DISPLAY="$XDISPLAY" xdotool mousemove --sync 1020 650 click 1 >/dev/null 2>&1 || exit 1
sleep .3
DISPLAY="$XDISPLAY" xdotool mousemove --sync 915 650 click 1 >/dev/null 2>&1 || exit 1
sleep 1
# Search field and loop filter: x=120, y=115.
DISPLAY="$XDISPLAY" xdotool mousemove --sync 120 115 click 1 >/dev/null 2>&1 || exit 1
DISPLAY="$XDISPLAY" xdotool type --delay 20 loop >/dev/null 2>&1 || exit 1
sleep .4
DISPLAY="$XDISPLAY" xwd -id "$WIN" -silent 2>/dev/null \
  | convert xwd:- "$OUT/syntax-studio-ui.png" 2>/dev/null || true
kill -9 "$APID" 2>/dev/null || true
APID=0
[ -f "$TRACE" ] || { echo SYNTAX_STUDIO_FAIL trace-missing; exit 1; }
grep -q 'lesson id=2 title=Conditions' "$TRACE" || { echo SYNTAX_STUDIO_FAIL selection; cat "$TRACE"; exit 1; }
grep -q 'check result=pass ideas=4' "$TRACE" || { echo SYNTAX_STUDIO_FAIL check; cat "$TRACE"; exit 1; }
grep -q 'run exit=0' "$TRACE" || { echo SYNTAX_STUDIO_FAIL run; cat "$TRACE"; exit 1; }
grep -q 'filter query=loop count=1' "$TRACE" || { echo SYNTAX_STUDIO_FAIL filter; cat "$TRACE"; exit 1; }
grep -q 'lesson id=3 title=Loops' "$TRACE" || { echo SYNTAX_STUDIO_FAIL filtered-lesson; cat "$TRACE"; exit 1; }
test -s /tmp/ahk-studio-exercise.txt || { echo SYNTAX_STUDIO_FAIL exercise-output; exit 1; }
cat > "$OUT/syntax-studio-ui-summary.json" <<EOF
{"schema":1,"result":"pass","window":"AHK v2 Syntax Studio","selection":"Conditions","check":"pass","run_exit":0,"filter":"loop","filtered_lesson":"Loops","screenshot":"syntax-studio-ui.png"}
EOF
echo 'SYNTAX_STUDIO_UI_PASS selection=2 check=pass run=0 filter=loop filtered=3'
