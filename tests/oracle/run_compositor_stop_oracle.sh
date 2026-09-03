#!/bin/bash
# M6 fault injection: SIGSTOP a private sway compositor for 3 seconds. The AHK
# Wayland client must keep timers running and remain usable after SIGCONT.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
for command in sway swaymsg; do
  command -v "$command" >/dev/null || { echo "COMPOSITOR_STOP_SKIP missing-$command"; exit 2; }
done
RUNTIME="/tmp/ahk-compositor-stop-$$"
mkdir -m 700 "$RUNTIME"
cat >"$RUNTIME/config" <<'EOF'
seat * xcursor_theme empty
input "*" xkb_layout us
output "*" bg #000000 solid_color
xwayland disable
bindsym b exec touch /tmp/ahk_compositor_key_before
bindsym a exec touch /tmp/ahk_compositor_key_after
EOF
SWAYPID=0
APID=0
cleanup() {
  [ "$SWAYPID" = 0 ] || kill -CONT "$SWAYPID" 2>/dev/null || true
  [ "$APID" = 0 ] || kill "$APID" 2>/dev/null || true
  [ "$SWAYPID" = 0 ] || kill "$SWAYPID" 2>/dev/null || true
  [ "$APID" = 0 ] || wait "$APID" 2>/dev/null || true
  [ "$SWAYPID" = 0 ] || wait "$SWAYPID" 2>/dev/null || true
  rm -rf "$RUNTIME"
}
trap cleanup EXIT HUP INT TERM
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
  XDG_RUNTIME_DIR="$RUNTIME" sway -c "$RUNTIME/config" >"$RUNTIME/sway.log" 2>&1 &
SWAYPID=$!
wayland_socket=""
sway_socket=""
for _ in $(seq 1 200); do
  wayland_socket=$(find "$RUNTIME" -maxdepth 1 -type s -name 'wayland-*' -printf '%f\n' | head -1)
  sway_socket=$(find "$RUNTIME" -maxdepth 1 -type s -name 'sway-ipc.*.sock' -print | head -1)
  [ -n "$wayland_socket" ] && [ -n "$sway_socket" ] && break
  sleep .02
done
[ -n "$wayland_socket" ] && [ -n "$sway_socket" ] \
  || { echo COMPOSITOR_STOP_START_FAIL; cat "$RUNTIME/sway.log"; exit 1; }

cat >/tmp/ahk_compositor_stop.ahk <<'EOF'
#Requires AutoHotkey v2.0
global stopTicks := 0
StopTick() {
    global stopTicks
    stopTicks += 1
    Send("{F24}") ; exercise the existing virtual-keyboard connection
    FileAppend(stopTicks "`n", "/tmp/ahk_compositor_stop_ticks")
}
CheckContinue() {
    global stopTicks
    if !FileExist("/tmp/ahk_compositor_continue")
        return
    SetTimer(CheckContinue, 0)
    Send("a")
    FileAppend("after=" stopTicks "`n", "/tmp/ahk_compositor_after")
    SetTimer(FinishStopTest, -1200)
}
FinishStopTest() {
    global stopTicks
    FileAppend("done=" stopTicks "`n", "/tmp/ahk_compositor_done")
    ExitApp
}
Send("b")
SetTimer(StopTick, 100)
SetTimer(CheckContinue, 50)
FileAppend("ready`n", "/tmp/ahk_compositor_ready")
SetTimer(() => ExitApp(9), -15000)
EOF
rm -f /tmp/ahk_compositor_{ready,ticks,continue,after,done,tree,runtime.log,key_before,key_after}
env -u DISPLAY AHK_LIBEI=0 XDG_SESSION_TYPE=wayland XDG_RUNTIME_DIR="$RUNTIME" \
  WAYLAND_DISPLAY="$wayland_socket" "$BIN" /tmp/ahk_compositor_stop.ahk \
  >/tmp/ahk_compositor_runtime.log 2>&1 &
APID=$!
for _ in $(seq 1 200); do test -f /tmp/ahk_compositor_ready && break; sleep .02; done
test -f /tmp/ahk_compositor_ready \
  || { echo COMPOSITOR_STOP_CLIENT_START_FAIL; cat /tmp/ahk_compositor_runtime.log; exit 1; }
for _ in $(seq 1 200); do test -f /tmp/ahk_compositor_key_before && break; sleep .02; done
test -f /tmp/ahk_compositor_key_before \
  || { echo COMPOSITOR_STOP_PRE_INPUT_FAIL; cat /tmp/ahk_compositor_runtime.log; exit 1; }
sleep 1
before=$(wc -l </tmp/ahk_compositor_stop_ticks)
kill -STOP "$SWAYPID"
sleep 3
kill -0 "$APID" 2>/dev/null \
  || { echo COMPOSITOR_STOP_CLIENT_DIED; cat /tmp/ahk_compositor_runtime.log; exit 1; }
during=$(wc -l </tmp/ahk_compositor_stop_ticks)
delta=$((during - before))
[ "$delta" -ge 20 ] \
  || { echo "COMPOSITOR_STOP_TIMER_STALLED before=$before during=$during"; exit 1; }
kill -CONT "$SWAYPID"
touch /tmp/ahk_compositor_continue
for _ in $(seq 1 200); do test -f /tmp/ahk_compositor_after && break; sleep .02; done
test -f /tmp/ahk_compositor_after || { echo COMPOSITOR_CONT_CALLBACK_FAIL; exit 1; }
for _ in $(seq 1 200); do test -f /tmp/ahk_compositor_key_after && break; sleep .02; done
test -f /tmp/ahk_compositor_key_after \
  || { echo COMPOSITOR_CONT_INPUT_FAIL; cat /tmp/ahk_compositor_runtime.log; exit 1; }
SWAYSOCK="$sway_socket" swaymsg -t get_tree >/tmp/ahk_compositor_tree 2>&1
[ "$?" = 0 ] && grep -q '"type": "root"' /tmp/ahk_compositor_tree \
  || { echo COMPOSITOR_CONT_IPC_FAIL; cat /tmp/ahk_compositor_tree; exit 1; }
wait "$APID"; arc=$?
APID=0
[ "$arc" = 0 ] && grep -q '^done=' /tmp/ahk_compositor_done \
  || { echo "COMPOSITOR_CONT_RUNTIME_FAIL rc=$arc"; cat /tmp/ahk_compositor_runtime.log; exit 1; }
after=$(sed -n 's/^after=//p' /tmp/ahk_compositor_after)
done_ticks=$(sed -n 's/^done=//p' /tmp/ahk_compositor_done)
cat >"$OUT/compositor-stop-summary.json" <<EOF
{"schema":1,"result":"pass","compositor":"sway-headless","stop_seconds":3,"ticks_before":$before,"ticks_during_end":$during,"ticks_while_stopped":$delta,"ticks_after_resume":$after,"ticks_done":$done_ticks,"input_before_stop":true,"input_after_resume":true,"ipc_after_resume":true,"runtime_pid_stable":true}
EOF
echo "COMPOSITOR_STOP_ORACLE_PASS stop_s=3 ticks_while_stopped=$delta input_after_resume=1 ipc_after_resume=1 runtime_pid_stable=1"
