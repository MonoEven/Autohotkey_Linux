#!/bin/bash
# M5 IME: real XWayland GTK + IBus libpinyin + XTEST keys. AHK independently
# observes XI2 physical keys and eavesdropped IBus preedit/commit signals.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
for command in ibus xdotool pkg-config; do
  command -v "$command" >/dev/null || { echo "IBUS_IME_SKIP missing-$command"; exit 2; }
done
pkg-config --exists gtk+-3.0 || { echo IBUS_IME_SKIP gtk3-dev-missing; exit 2; }
${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/gtk-ok" "$ROOT/tests/oracle/gtk_ok.c" \
  $(pkg-config --cflags --libs gtk+-3.0) || exit 2

auth=$(ls -1t /run/user/$(id -u)/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
[ -n "$auth" ] || { echo IBUS_IME_SKIP xwayland-auth-missing; exit 2; }
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
display=$(ps -u "$(id -u)" -o args= | sed -n 's/.*Xwayland \(:[0-9][0-9]*\).*/\1/p' | head -1)
[ -n "$display" ] || { echo IBUS_IME_SKIP xwayland-display-missing; exit 2; }
export DISPLAY="$display"
export XAUTHORITY="$auth"
export GTK_IM_MODULE=ibus
export GDK_BACKEND=x11
export XDG_SESSION_TYPE=x11
prior_engine=$(ibus engine 2>/dev/null || true)
cleanup() {
  pkill -f 'gtk-ok --title IBus' 2>/dev/null || true
  [ -z "$prior_engine" ] || ibus engine "$prior_engine" >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM
ibus engine libpinyin >/dev/null 2>&1 || true
[ "$(ibus engine 2>/dev/null)" = libpinyin ] \
  || { echo IBUS_IME_SKIP libpinyin-unavailable; exit 2; }

cat >/tmp/ime_hotstring.ahk <<'EOF'
#Requires AutoHotkey v2.0
ImeHit(*) {
    s := ImeStatus()
    FileAppend("hit=1 preedit=" s.Preedit " commits=" s.Commits " last=" s.LastCommit "`n", "/tmp/ime_hotstring_hit")
    ExitApp
}
Decoy(*) {
    FileAppend("decoy=1`n", "/tmp/ime_hotstring_decoy")
    ExitApp(6)
}
Hotstring(":B0*:n", Decoy)
Hotstring(":*:你好", ImeHit)
FileAppend("ready=auto`n", "/tmp/ime_hotstring_ready")
SetTimer(() => ExitApp(3), -15000)
EOF
rm -f /tmp/ime_hotstring_ready /tmp/ime_hotstring_hit /tmp/ime_hotstring_decoy /tmp/ime_hotstring_dump /tmp/ime_hotstring_trace
AHK_IME_DUMP=/tmp/ime_hotstring_dump AHK_INPUT_EVENT_TRACE=/tmp/ime_hotstring_trace \
  "$BIN" /tmp/ime_hotstring.ahk >/tmp/ime_hotstring_ahk.log 2>&1 &
APID=$!
for _ in $(seq 1 200); do
  test -f /tmp/ime_hotstring_ready && grep -q '^listener.*text=libpinyin$' /tmp/ime_hotstring_dump 2>/dev/null && break
  sleep .05
done
grep -q '^ready=auto$' /tmp/ime_hotstring_ready \
  && grep -q '^listener.*text=libpinyin$' /tmp/ime_hotstring_dump \
  || { echo IBUS_STATUS_FAIL; cat /tmp/ime_hotstring_ready /tmp/ime_hotstring_dump /tmp/ime_hotstring_ahk.log; exit 1; }
"$OUT/gtk-ok" --title IBusHotstring --name IBUS-HOTSTRING-ENTRY --text '' >/tmp/ime_hotstring_gtk.log 2>&1 &
GPID=$!
sleep 2
wid=$(xdotool search --name IBusHotstring | tail -1)
[ -n "$wid" ] || { echo IBUS_HOTSTRING_WINDOW_FAIL; exit 1; }
xdotool windowactivate --sync "$wid"
xdotool click --window "$wid" 1
sleep .3
xdotool type --delay 120 nihao
xdotool key space
set +e
wait "$APID"; hot_rc=$?
set -e
kill "$GPID" 2>/dev/null; wait "$GPID" 2>/dev/null || true
[ "$hot_rc" = 0 ] && [ ! -e /tmp/ime_hotstring_decoy ] \
  && grep -q '^hit=1 preedit=0 commits=1 last=你好$' /tmp/ime_hotstring_hit \
  || { echo IBUS_HOTSTRING_FAIL; cat /tmp/ime_hotstring_decoy /tmp/ime_hotstring_hit /tmp/ime_hotstring_dump /tmp/ime_hotstring_ahk.log; exit 1; }

cat >/tmp/ime_inputhook.ahk <<'EOF'
#Requires AutoHotkey v2.0
global imeChars := ""
ImeChar(_, char) {
    global imeChars
    imeChars .= char
}
ih := InputHook("V T15", "", "a,你好")
ih.OnChar := ImeChar
ih.Start()
s := ImeStatus()
FileAppend("ready=" s.Framework ":" s.Engine ":" s.Listening ":" s.Scope "`n", "/tmp/ime_inputhook_ready")
ih.Wait()
s := ImeStatus()
FileAppend("input=" ih.Input " reason=" ih.EndReason " match=" ih.Match " chars=" imeChars " preedit=" s.Preedit " commits=" s.Commits " last=" s.LastCommit "`n", "/tmp/ime_inputhook_result")
ExitApp(ih.EndReason = "Match" && ih.Input = "你好" && imeChars = "你好" ? 0 : 4)
EOF
rm -f /tmp/ime_inputhook_ready /tmp/ime_inputhook_result /tmp/ime_inputhook_dump /tmp/ime_inputhook_trace
AHK_IME_DUMP=/tmp/ime_inputhook_dump AHK_INPUT_EVENT_TRACE=/tmp/ime_inputhook_trace \
  "$BIN" /tmp/ime_inputhook.ahk >/tmp/ime_inputhook_ahk.log 2>&1 &
APID=$!
for _ in $(seq 1 200); do test -f /tmp/ime_inputhook_ready && break; sleep .05; done
grep -q '^ready=ibus:libpinyin:1:eavesdrop$' /tmp/ime_inputhook_ready \
  || { echo IBUS_INPUTHOOK_STATUS_FAIL; cat /tmp/ime_inputhook_ready; exit 1; }
"$OUT/gtk-ok" --title IBusInputHook --name IBUS-INPUTHOOK-ENTRY --text '' >/tmp/ime_inputhook_gtk.log 2>&1 &
GPID=$!
sleep 2
wid=$(xdotool search --name IBusInputHook | tail -1)
[ -n "$wid" ] || { echo IBUS_INPUTHOOK_WINDOW_FAIL; exit 1; }
xdotool windowactivate --sync "$wid"
xdotool click --window "$wid" 1
sleep .3
# Cancel one preedit after an actual Backspace. These phonetic edits must never
# enter or undo InputHook.Input. Then commit the independent Chinese trigger.
xdotool type --delay 120 abc
xdotool key BackSpace
xdotool type --delay 120 d
xdotool key Escape
sleep .4
xdotool type --delay 120 nihao
xdotool key space
set +e
wait "$APID"; input_rc=$?
set -e
[ "$input_rc" = 0 ] && grep -q '^input=你好 reason=Match match=你好 chars=你好 preedit=0 commits=1 last=你好$' /tmp/ime_inputhook_result \
  || { echo IBUS_INPUTHOOK_FAIL; cat /tmp/ime_inputhook_result /tmp/ime_inputhook_dump /tmp/ime_inputhook_ahk.log; exit 1; }
commit_count=$(grep -c '"source":"ime_commit"' /tmp/ime_inputhook_trace 2>/dev/null || true)
[ "$commit_count" = 2 ] \
  && grep -q '"text":20320.*"source":"ime_commit".*"origin":"ibus"' /tmp/ime_inputhook_trace \
  && grep -q '"text":22909.*"source":"ime_commit".*"origin":"ibus"' /tmp/ime_inputhook_trace \
  || { echo IBUS_EVENT_MODEL_FAIL; cat /tmp/ime_inputhook_trace; exit 1; }
preedit_visible=$(grep -c '^preedit.*visible=1' /tmp/ime_inputhook_dump 2>/dev/null || true)
preedit_hidden=$(grep -c '^preedit.*visible=0' /tmp/ime_inputhook_dump 2>/dev/null || true)
focus_in=$(grep -c '^focus-in.*InputContext_' /tmp/ime_inputhook_dump 2>/dev/null || true)
[ "$preedit_visible" -ge 2 ] && [ "$preedit_hidden" -ge 2 ] && [ "$focus_in" -ge 1 ] \
  || { echo IBUS_PREEDIT_TRANSITION_FAIL; cat /tmp/ime_inputhook_dump; exit 1; }

# Independent target-state proof: after abc/Escape cancellation, GTK contains
# only the committed Chinese text (the InputHook is visible/non-suppressing).
cat >/tmp/ime_target_read.ahk <<'EOF'
#Requires AutoHotkey v2.0
FileAppend(ControlGetText("IBUS-INPUTHOOK-ENTRY", "IBusInputHook") "`n", "/tmp/ime_target_text")
ExitApp
EOF
rm -f /tmp/ime_target_text
XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-0 DISPLAY= \
  "$BIN" /tmp/ime_target_read.ahk >/tmp/ime_target_read.log 2>&1
kill "$GPID" 2>/dev/null; wait "$GPID" 2>/dev/null || true
grep -q '^你好$' /tmp/ime_target_text \
  || { echo IBUS_TARGET_TEXT_FAIL; cat /tmp/ime_target_text /tmp/ime_target_read.log; exit 1; }

# A composing engine can still pass characters through without preedit. Prove
# the 500ms race window is bounded: physical KeyDown is immediate, then Input,
# OnChar and Match recover rather than swallowing the direct digit.
cat >/tmp/ime_passthrough.ahk <<'EOF'
#Requires AutoHotkey v2.0
global keyTick := 0, charTick := 0, passChars := ""
PassKey(_, vk, *) {
    global keyTick
    if vk = 0x31
        keyTick := A_TickCount
}
PassChar(_, char) {
    global charTick, passChars
    charTick := A_TickCount
    passChars .= char
}
ih := InputHook("V T5", "", "1")
ih.OnKeyDown := PassKey
ih.OnChar := PassChar
ih.Start()
s := ImeStatus()
FileAppend("ready=" s.Framework ":" s.Engine ":" s.Listening "`n", "/tmp/ime_passthrough_ready")
ih.Wait()
elapsed := keyTick && charTick ? charTick - keyTick : -1
FileAppend("input=" ih.Input " match=" ih.Match " chars=" passChars " elapsed=" elapsed "`n", "/tmp/ime_passthrough_result")
ExitApp(ih.EndReason = "Match" && ih.Input = "1" && passChars = "1" && elapsed >= 400 && elapsed < 2000 ? 0 : 5)
EOF
rm -f /tmp/ime_passthrough_ready /tmp/ime_passthrough_result
"$BIN" /tmp/ime_passthrough.ahk >/tmp/ime_passthrough_ahk.log 2>&1 &
APID=$!
for _ in $(seq 1 200); do test -f /tmp/ime_passthrough_ready && break; sleep .05; done
"$OUT/gtk-ok" --title IBusPassthrough --name IBUS-PASSTHROUGH-ENTRY --text '' >/tmp/ime_passthrough_gtk.log 2>&1 &
GPID=$!
sleep 2
wid=$(xdotool search --name IBusPassthrough | tail -1)
[ -n "$wid" ] || { echo IBUS_PASSTHROUGH_WINDOW_FAIL; exit 1; }
xdotool windowactivate --sync "$wid"
xdotool click --window "$wid" 1
sleep .3
xdotool key 1
set +e
wait "$APID"; pass_rc=$?
set -e
kill "$GPID" 2>/dev/null; wait "$GPID" 2>/dev/null || true
pass_elapsed=$(sed -n 's/.*elapsed=\([0-9][0-9]*\).*/\1/p' /tmp/ime_passthrough_result)
[ "$pass_rc" = 0 ] && grep -q '^input=1 match=1 chars=1 elapsed=' /tmp/ime_passthrough_result \
  || { echo IBUS_PASSTHROUGH_FAIL; cat /tmp/ime_passthrough_result /tmp/ime_passthrough_ahk.log; exit 1; }

cat >/tmp/ime_state_only.ahk <<'EOF'
#Requires AutoHotkey v2.0
s := ImeStatus()
FileAppend("state=" s.Framework ":" s.Listening ":" s.Scope ":" s.Engine "`n", "/tmp/ime_state_only_result")
ExitApp
EOF
rm -f /tmp/ime-state-only-missing /tmp/ime_state_only_result
IBUS_ADDRESS=unix:path=/tmp/ime-state-only-missing \
  "$BIN" /tmp/ime_state_only.ahk >/tmp/ime_state_only.log 2>&1
grep -q '^state=ibus:0:state-only:$' /tmp/ime_state_only_result \
  || { echo IBUS_STATE_ONLY_FAIL; cat /tmp/ime_state_only_result /tmp/ime_state_only.log; exit 1; }

cat >"$OUT/ibus-ime-summary.json" <<EOF
{"schema":1,"result":"pass","ibus_version":"$(ibus version | sed 's/^IBus //')","engine":"libpinyin","listener":"eavesdrop","listener_auto_start":true,"commit":"你好","hotstring":true,"inputhook":"你好","onchar":"你好","cancel_preedit_clean":true,"preedit_backspace_clean":true,"target_text":"你好","preedit_visible_events":$preedit_visible,"preedit_hidden_events":$preedit_hidden,"focused_context_events":$focus_in,"ime_event_count":$commit_count,"event_origin":"ibus","passthrough":"1","passthrough_delay_ms":$pass_elapsed,"state_only_fault":true}
EOF
echo "IBUS_IME_ORACLE_PASS engine=libpinyin commit=你好 hotstring=1 auto_listener=1 inputhook=你好 onchar=你好 cancel_clean=1 backspace_clean=1 focus=$focus_in passthrough_ms=$pass_elapsed state_only=1 events=$commit_count"
