#!/bin/bash
# SendLevel/InputLevel policy oracle (check0901 P0-2 / check_detail0901 §2).
#
# Verifies the official Windows v2.0.26 rules against the X11 lane:
#   - Hotkey/Hotstring trigger only when send_level > input_level (strict).
#   - Equal levels never fire; physical/non-AHK input is never filtered.
#   - Hotkey/hotstring threads start with SendLevel = their InputLevel.
#   - Hotstring buffer collects levels > 0 only; level-0 chars are invisible.
#   - Hotstring output is generated at level 0 (no recursion).
#   - SendInput (and Send/SendText under default SendMode "Input") never fire
#     the script's own hook hotkeys, but the target still receives the key.
#   - SendPlay never fires own hotkeys/hotstrings/InputHook.
#   - InputHook MinSendLevel: SendEvent-class collects at level >= min;
#     SendInput/SendPlay are always ignored.
#
# This oracle asserts the documented Windows golden (documented-semantics
# evidence, not a fresh Windows differential run).
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
SUMMARY="$OUT/sendlevel-policy-summary.json"

command -v Xvfb >/dev/null || { echo "Xvfb missing"; exit 1; }
command -v xdotool >/dev/null || { echo "xdotool missing"; exit 1; }
command -v xev >/dev/null || { echo "xev missing"; exit 1; }

DISP=:96
Xvfb "$DISP" -screen 0 1280x1024x24 >/tmp/sl-policy-xvfb.log 2>&1 &
XVFB_PID=$!
cleanup() {
  kill "$XVFB_PID" 2>/dev/null || true
  wait "$XVFB_PID" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
export DISPLAY=$DISP
for _ in $(seq 1 100); do xdpyinfo >/dev/null 2>&1 && break; sleep .05; done
xdpyinfo >/dev/null

RESULT=/tmp/sl_policy_result.txt
PASS=0
FAIL=0
FAILURES=""

# run_case <name> <expected> <ahk_body> <drive_fn>
run_case() {
  local name="$1" expected="$2" body="$3" drive="$4"
  rm -f "$RESULT" /tmp/sl_policy_ready /tmp/xev_keyboard.log
  cat >/tmp/sl_policy_case.ahk <<EOF
#Requires AutoHotkey v2.0
$body
EOF
  "$BIN" /tmp/sl_policy_case.ahk >/tmp/sl_policy_case.log 2>&1 &
  local ahk_pid=$!
  local ready=0
  for _ in $(seq 1 300); do [ -f /tmp/sl_policy_ready ] && { ready=1; break; }; sleep .02; done
  if [ "$ready" = 0 ]; then
    echo "FAIL $name (script did not become ready)"
    cat /tmp/sl_policy_case.log
    FAIL=$((FAIL+1)); FAILURES="$FAILURES $name(no-ready)"
    kill "$ahk_pid" 2>/dev/null || true
    wait "$ahk_pid" 2>/dev/null || true
    return
  fi
  sleep .15
  "$drive"
  wait "$ahk_pid" 2>/dev/null || true
  local actual=""
  [ -f "$RESULT" ] && actual=$(cat "$RESULT")
  if [ "$actual" = "$expected" ]; then
    PASS=$((PASS+1))
    echo "PASS $name [$actual]"
  else
    FAIL=$((FAIL+1)); FAILURES="$FAILURES $name(got=[$actual])"
    echo "FAIL $name expected=[$expected] got=[$actual]"
    cat /tmp/sl_policy_case.log
  fi
}

# Rapid xdotool "key" (press+release nearly atomic) races the injected
# event's self/passthru classification; drive with explicit keydown/keyup
# and settle sleeps so each event round-trips through the X server before
# the next one is injected.
press() {
  local k="$1"
  xdotool keydown "$k"
  sleep .08
  xdotool keyup "$k"
  sleep .12
}
press_seq() { for k in "$@"; do press "$k"; done; }
press_F1() { press F1; }
drive_hotstring_physical_q() { press q; }
drive_physical_w() { press w; }
wait_short() { sleep .3; }
drive_sendinput_xev() {
  xev -event keyboard >/tmp/xev_keyboard.log 2>&1 &
  local xev_pid=$!
  sleep .3
  local w=""
  for _ in $(seq 1 100); do
    w=$(xdotool search --name "Event Tester" 2>/dev/null | tail -1 || true)
    [ -n "$w" ] && break
    sleep .03
  done
  xdotool windowfocus --sync "$w" 2>/dev/null || true
  press F1
  sleep .6
  kill "$xev_pid" 2>/dev/null || true
  wait "$xev_pid" 2>/dev/null || true
  if ! grep -q "keysym 0x61" /tmp/xev_keyboard.log; then
    echo "FAIL sendinput_target_delivery: xev did not see 'a'"
    FAIL=$((FAIL+1)); FAILURES="$FAILURES sendinput_target_delivery(xev)"
    tail -20 /tmp/xev_keyboard.log
  fi
}

# Common scaffolding: F1..F5 drive the matrix; a timer reports "nofire" when
# nothing fired and exits the script.
SCAFFOLD_FIRE='NofireTimer() {
    if (!FileExist("/tmp/sl_policy_result.txt"))
        FileAppend("nofire`n", "/tmp/sl_policy_result.txt")
    ExitApp()
}
FileDelete("/tmp/sl_policy_result.txt")
SetTimer(NofireTimer, -900)
FileAppend("ready`n", "/tmp/sl_policy_ready")
'

# --- Hotkey matrix (strict send_level > input_level) ------------------------
run_case hotkey_eq_00 nofire "$SCAFFOLD_FIRE
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(0)
    SendEvent('a')
}" press_F1

run_case hotkey_gt_1_0 fire "$SCAFFOLD_FIRE
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(1)
    SendEvent('a')
}" press_F1

run_case hotkey_eq_55 nofire "$SCAFFOLD_FIRE
#InputLevel 5
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(5)
    SendEvent('a')
}" press_F1

run_case hotkey_gt_10_5 fire "$SCAFFOLD_FIRE
#InputLevel 5
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(10)
    SendEvent('a')
}" press_F1

run_case hotkey_lt_5_10 nofire "$SCAFFOLD_FIRE
#InputLevel 10
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(5)
    SendEvent('a')
}" press_F1

# Hotkey thread starts at its InputLevel: F1 sends 'a' at level 4 -> a
# (level 3) fires; a's thread starts at SendLevel 3 and sends 'c' -> c
# (level 2) fires because 3 > 2.
run_case hotkey_thread_inherits_level fire "$SCAFFOLD_FIRE
#InputLevel 3
a:: {
    SendEvent('c')
}
#InputLevel 2
c:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(4)
    SendEvent('a')
}" press_F1

# SendPlay never fires own hotkeys (journal semantics).
run_case hotkey_sendplay_never nofire "$SCAFFOLD_FIRE
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(10)
    SendPlay('a')
}" press_F1

# SendInput / Send (default Input mode) / SendText(Input) never fire own
# hook hotkeys even at SendLevel 10.
run_case hotkey_sendinput_never nofire "$SCAFFOLD_FIRE
#InputLevel 5
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(10)
    SendInput('a')
}" press_F1

run_case hotkey_send_inputmode_never nofire "$SCAFFOLD_FIRE
#InputLevel 5
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(10)
    Send('a')
}" press_F1

run_case hotkey_sendtext_input_never nofire "$SCAFFOLD_FIRE
#InputLevel 5
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(10)
    SendText('a')
}" press_F1

# SendText follows SendMode: under SendMode "Event" it is level-gated and
# fires (SendLevel 1 > InputLevel 0).
run_case hotkey_sendtext_event_fires fire "$SCAFFOLD_FIRE
SendMode('Event')
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(1)
    SendText('a')
}" press_F1

# --- SendInput target delivery (re-injection) -------------------------------
run_case sendinput_target_delivery nofire "$SCAFFOLD_FIRE
a:: {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(10)
    SendInput('a')
}" drive_sendinput_xev

# --- Hotstring matrix -------------------------------------------------------
run_case hotstring_gt_1_0 fire "$SCAFFOLD_FIRE
:*X:q::HsFire
HsFire(*) {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(1)
    SendEvent('q')
}" press_F1

run_case hotstring_eq_55 nofire "$SCAFFOLD_FIRE
#InputLevel 5
:*X:q::HsFire
HsFire(*) {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(5)
    SendEvent('q')
}" press_F1

# Level-0 synthetic chars are invisible to the hotstring buffer.
run_case hotstring_level0_invisible nofire "$SCAFFOLD_FIRE
:*X:q::HsFire
HsFire(*) {
    FileAppend('fire\`n', '/tmp/sl_policy_result.txt')
}
F1:: {
    SendLevel(0)
    SendEvent('q')
}" press_F1

# Hotstring callback output at thread SendLevel 0 must not re-fire itself.
run_case hotstring_output_level0_norecurse "fire=1" 'HsCount := 0
:*X:q::HsFire
HsFire(*) {
    global HsCount
    HsCount++
    if (HsCount = 1)
        SendEvent("q")
}
HsReportTimer() {
    FileAppend("fire=" HsCount "`n", "/tmp/sl_policy_result.txt")
    ExitApp()
}
FileDelete("/tmp/sl_policy_result.txt")
SetTimer(HsReportTimer, -1600)
FileAppend("ready`n", "/tmp/sl_policy_ready")
' drive_hotstring_physical_q

# --- InputHook MinSendLevel matrix -------------------------------------------
# The self-sent characters are injected from a TIMER thread (no trigger key
# is held): while the script's own hotkey keeps an active keyboard grab,
# XTEST-injected events bypass the script's own passive capture grabs (an
# X11-lane boundary), so hotkey-driven injection would make these cases
# dependent on grab timing instead of the MinSendLevel policy.
IH_SCAFFOLD='IhDoneTimer() {
    ExitApp()
}
FileDelete("/tmp/sl_policy_result.txt")
ih := InputHook("I5")
ih.OnChar := (hook, char) => FileAppend(char, "/tmp/sl_policy_result.txt")
ih.Start()
SetTimer(IhDoneTimer, -1500)
FileAppend("ready`n", "/tmp/sl_policy_ready")
'

run_case ih_min4_ignored "" "$IH_SCAFFOLD
IhCaseTimer() {
    SendLevel(4)
    SendEvent('w')
}
SetTimer(IhCaseTimer, -500)" wait_short

run_case ih_min5_collect w "$IH_SCAFFOLD
IhCaseTimer() {
    SendLevel(5)
    SendEvent('w')
}
SetTimer(IhCaseTimer, -500)" wait_short

run_case ih_min6_collect w "$IH_SCAFFOLD
IhCaseTimer() {
    SendLevel(6)
    SendEvent('w')
}
SetTimer(IhCaseTimer, -500)" wait_short

run_case ih_sendinput_ignored "" "$IH_SCAFFOLD
IhCaseTimer() {
    SendLevel(10)
    SendInput('w')
}
SetTimer(IhCaseTimer, -500)" wait_short

run_case ih_sendplay_ignored "" "$IH_SCAFFOLD
IhCaseTimer() {
    SendLevel(10)
    SendPlay('w')
}
SetTimer(IhCaseTimer, -500)" wait_short

run_case ih_physical_collect w "$IH_SCAFFOLD" drive_physical_w

if [ "$FAIL" = 0 ]; then
  cat >"$SUMMARY" <<EOF
{"schema":1,"result":"pass","pass":$PASS,"fail":0,"scope":"sendlevel-inputlevel-windows-golden"}
EOF
else
  cat >"$SUMMARY" <<EOF
{"schema":1,"result":"fail","pass":$PASS,"fail":$FAIL,"failures":"$FAILURES"}
EOF
fi
echo "SENDLEVEL_POLICY_ORACLE_RESULT pass=$PASS fail=$FAIL summary=$SUMMARY"
[ "$FAIL" = 0 ]
