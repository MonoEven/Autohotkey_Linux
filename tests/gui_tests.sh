#!/bin/bash
# GUI-mode validation suite: runs with the X11 display available (WSLg/XFCE).
# All MsgBox calls use the T<seconds> timeout so dialogs auto-dismiss.
# Usage: gui_tests.sh [path-to-ahk_core]
cd "$(dirname "$0")/.." || exit 1
BIN=${1:-build-core/source/linux/core/ahk_core}
mkdir -p /tmp/ahk_t

if [ -z "$DISPLAY" ]; then
  echo "SKIP: no DISPLAY available (run under WSLg or an X server)."
  exit 0
fi

pass=0; fail=0
check() { # $1 = name, $2 = expected exit code, rest = args...
  local name="$1"; local want="$2"; shift 2
  timeout 60 "$BIN" "$@" > /tmp/ahk_t/out.txt 2>&1
  local got=$?
  if [ "$got" = "$want" ]; then pass=$((pass+1)); echo "PASS: $name (exit=$got)";
  else fail=$((fail+1)); echo "FAIL: $name (exit=$got, want=$want)"; cat /tmp/ahk_t/out.txt; fi
}

cat > /tmp/ahk_t/g01.ahk <<'EOF'
r := MsgBox("Timed X11 dialog", "AHK Linux", "T1")
MsgBox "r=" r, , "T1"
EOF
cat > /tmp/ahk_t/g02.ahk <<'EOF'
r := MsgBox("Yes/No buttons", , "YN T1")
MsgBox "yn=" r, , "T1"
r2 := MsgBox("OK/Cancel", , "OC T1")
MsgBox "oc=" r2, , "T1"
r3 := MsgBox("Yes/No/Cancel", , "YNC T1")
MsgBox "ync=" r3, , "T1"
EOF
cat > /tmp/ahk_t/g03.ahk <<'EOF'
ib := InputBox("Type something:", "AHK Input", , "default")
MsgBox "value=[" ib.Value "] result=" ib.Result, , "T1"
EOF
cat > /tmp/ahk_t/g04.ahk <<'EOF'
MsgBox "Run + dialog", , "T1"
Run "sleep 1"
MsgBox "after run", , "T1"
code := RunWait("/bin/echo gui-runwait-ok")
MsgBox "rw=" code, , "T1"
EOF
cat > /tmp/ahk_t/g05.ahk <<'EOF'
MsgBox A_YYYY "-" A_MM "-" A_DD, , "T1"
MsgBox A_Now, , "T1"
MsgBox DateAdd("20240101", 1, "days"), , "T1"
EOF
cat > /tmp/ahk_t/g06.ahk <<'EOF'
; Dialogs in a loop (multiple sequential X11 windows)
loop 3
    MsgBox "iteration " A_Index, , "T1"
MsgBox "done", , "T1"
EOF

check g01_timed_msgbox 0 /tmp/ahk_t/g01.ahk
check g02_button_sets 0 /tmp/ahk_t/g02.ahk
# g03 exercises the X11 input dialog; auto-confirm via the test hook.
AHK_INPUTBOX_AUTOCLOSE_MS=600 timeout 60 "$BIN" /tmp/ahk_t/g03.ahk > /tmp/ahk_t/out.txt 2>&1
if [ "$?" = 0 ]; then pass=$((pass+1)); echo "PASS: g03_inputbox_x11 (exit=0)";
else fail=$((fail+1)); echo "FAIL: g03_inputbox_x11"; cat /tmp/ahk_t/out.txt; fi
check g04_run_dialogs 0 /tmp/ahk_t/g04.ahk
check g05_time_dialogs 0 /tmp/ahk_t/g05.ahk
check g06_loop_dialogs 0 /tmp/ahk_t/g06.ahk

echo "=============================="
echo "GUI PASS=$pass FAIL=$fail"
[ "$fail" = 0 ]
