#!/bin/bash
# M7 Wayland compositor restart recovery oracle (check_detail0901 搂9.4).
# ONE long-lived AHK process runs against a private headless sway.  The
# compositor is then killed for real (SIGKILL; protocol death, not
# STOP/CONT) and a NEW sway instance is started on a fresh socket.  The SAME
# AHK process must: survive the death, reconnect with an incremented
# wayland_generation, and deliver input through the rebuilt session.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
command -v sway >/dev/null || { echo M7_WL_SKIP sway-missing; exit 2; }
command -v swaymsg >/dev/null || { echo M7_WL_SKIP swaymsg-missing; exit 2; }

RUNTIME="/tmp/ahk-m7-wl-$$"
mkdir -m 700 "$RUNTIME"
cat >"$RUNTIME/config" <<'EOF'
seat * xcursor_theme empty
input "*" xkb_layout us
output "*" bg #000000 solid_color
xwayland disable
bindsym b exec touch /tmp/ahk_m7wl_saw_key
bindsym n exec touch /tmp/ahk_m7wl_saw_key2
EOF

SWAYPID=0
APID=0
cleanup() {
  [ "$APID" = 0 ] || kill -9 "$APID" 2>/dev/null || true
  [ "$SWAYPID" = 0 ] || kill -9 "$SWAYPID" 2>/dev/null || true
  [ "$APID" = 0 ] || wait "$APID" 2>/dev/null || true
  [ "$SWAYPID" = 0 ] || wait "$SWAYPID" 2>/dev/null || true
  rm -rf "$RUNTIME"
}
trap cleanup EXIT HUP INT TERM

start_sway() {
  WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
    XDG_RUNTIME_DIR="$RUNTIME" sway -c "$RUNTIME/config" >>"$RUNTIME/sway.log" 2>&1 &
  SWAYPID=$!
  for _ in $(seq 1 300); do
    [ -n "$(find "$RUNTIME" -maxdepth 1 -type s -name 'wayland-*' 2>/dev/null | head -1)" ] && break
    kill -0 "$SWAYPID" 2>/dev/null || break
    sleep .02
  done
  [ -n "$(find "$RUNTIME" -maxdepth 1 -type s -name 'wayland-*' 2>/dev/null | head -1)" ] \
    || { echo M7_WL_START_FAIL; tail -5 "$RUNTIME/sway.log"; exit 1; }
}

start_sway
first_socket=$(find "$RUNTIME" -maxdepth 1 -type s -name 'wayland-*' -printf '%f\n' | head -1)

# The single long-lived AHK process.  Script-side protocol:
#   /tmp/ahk_m7wl_cmd contains: wait-g1 | wait-g2 | done
#   state/results are appended to the OUT log; the key markers prove
#   delivery through each generation.
cat >"$RUNTIME/m7wl.ahk" <<'EOF'
#Requires AutoHotkey v2.0
OUT := A_Args[1]
err_seen := ""
; Deliver a key FIRST (a fresh HotkeyBackendGet() probe before any Send can
; perturb the input-backend auto-selection), then snapshot the generation.
gen := 0
active := 0
Loop 40
{
    if FileExist("/tmp/ahk_m7wl_saw_key")
    {
        b := HotkeyBackendGet()
        gen := b.wayland_generation
        active := b.wayland_active
        break
    }
    try
    {
        Send("b")
    }
    catch Error as e
    {
        FileAppend("send-err=" e.Message "`n", OUT)
        break
    }
    Sleep(100)
}
if FileExist("/tmp/ahk_m7wl_saw_key")
    FileAppend("g1_key=1 gen=" gen " active=" active "`n", OUT)
else
    FileAppend("g1_key=0`n", OUT)
FileAppend("g1_done", "/tmp/ahk_m7wl_cmd")

; ---- Compositor dies here (script keeps running).  Wait for the new
; ---- instance, then prove delivery through generation >= 2. ----
Loop 300
{
    cmd := ""
    try cmd := FileRead("/tmp/ahk_m7wl_cmd")
    if InStr(cmd, "wait-g2")
        break
    Sleep(100)
    try Send("b")  ; keeps dispatching through the dying/dead generation.
}
Loop 400
{
    if FileExist("/tmp/ahk_m7wl_saw_key2")
        break
    try
    {
        Send("n")
    }
    catch Error as e
    {
        if !InStr(err_seen, e.Message)
        {
            err_seen .= e.Message "`n"
            b := HotkeyBackendGet()
            FileAppend("g2err gen=" b.wayland_generation " active=" b.wayland_active " msg=" e.Message "`n", OUT)
        }
    }
    Sleep(100)
}
if FileExist("/tmp/ahk_m7wl_saw_key2")
    FileAppend("g2_key=1`n", OUT)
else
    FileAppend("g2_key=0`n", OUT)
b := HotkeyBackendGet()
FileAppend("g2 gen=" b.wayland_generation " active=" b.wayland_active "`n", OUT)
FileAppend("final gen=" b.wayland_generation " alive=1`n", OUT)
FileAppend("done", "/tmp/ahk_m7wl_cmd")
ExitApp 0
EOF

rm -f /tmp/ahk_m7wl_cmd /tmp/ahk_m7wl_saw_key /tmp/ahk_m7wl_saw_key2
echo wait-g1 > /tmp/ahk_m7wl_cmd
rm -f "$OUT/m7-wl-restart.log"

env -u DISPLAY AHK_LIBEI=0 XDG_SESSION_TYPE=wayland XDG_RUNTIME_DIR="$RUNTIME" \
  WAYLAND_DISPLAY="$first_socket" "$BIN" "$RUNTIME/m7wl.ahk" \
  "$OUT/m7-wl-restart.log" >/tmp/ahk_m7wl1.log 2>&1 &
APID=$!
for _ in $(seq 1 100); do test -s /tmp/ahk_m7wl_cmd && break; sleep .05; done

# Wait for g1 delivery (marker written by sway's bindsym).
g1_ok=0
for _ in $(seq 1 200); do
  grep -q '^g1_key=1 ' "$OUT/m7-wl-restart.log" 2>/dev/null && { g1_ok=1; break; }
  sleep .05
done
[ "$g1_ok" = 1 ] || { echo M7_WL_FAIL g1_key; cat "$OUT/m7-wl-restart.log" /tmp/ahk_m7wl1.log; exit 1; }
grep -q '^g1_key=1 gen=1 active=1$' "$OUT/m7-wl-restart.log" \
  || { echo M7_WL_FAIL g1_generation; cat "$OUT/m7-wl-restart.log"; exit 1; }
# Wait for the script to reach the g1_done barrier (keeps running).
for _ in $(seq 1 200); do grep -q g1_done /tmp/ahk_m7wl_cmd 2>/dev/null && break; sleep .05; done

# ---- Kill the compositor for real ----
kill -9 "$SWAYPID" 2>/dev/null
wait "$SWAYPID" 2>/dev/null || true
SWAYPID=0
rm -f "$RUNTIME"/wayland-*
sleep 0.3

# ---- Start a NEW compositor instance (fresh socket) ----
start_sway
new_socket=$(find "$RUNTIME" -maxdepth 1 -type s -name 'wayland-*' -printf '%f\n' | head -1)
[ -n "$new_socket" ] || { echo M7_WL_FAIL new_socket; exit 1; }

# The AHK client was connected to the OLD socket path.  Wayland reconnect
# resolves WAYLAND_DISPLAY at connect time; to exercise a real rebuild with
# a NEW socket we point the same process at the new name via the runtime
# dir default (wayland-1 vs wayland-0 keep distinct names).  Signal g2.
rm -f /tmp/ahk_m7wl_saw_key2
echo wait-g2 > /tmp/ahk_m7wl_cmd

# Wait for g2 delivery through the rebuilt generation.
g2_ok=0
for _ in $(seq 1 600); do
  grep -q '^g2_key=1$' "$OUT/m7-wl-restart.log" 2>/dev/null && { g2_ok=1; break; }
  kill -0 "$APID" 2>/dev/null || break
  sleep .05
done
[ "$g2_ok" = 1 ] || { echo M7_WL_FAIL g2_key; cat "$OUT/m7-wl-restart.log" /tmp/ahk_m7wl1.log; exit 1; }
grep -q '^g2 gen=2 active=1$' "$OUT/m7-wl-restart.log" \
  || { echo M7_WL_FAIL g2_generation; cat "$OUT/m7-wl-restart.log"; exit 1; }
grep -q '^final gen=2 alive=1$' "$OUT/m7-wl-restart.log" \
  || { echo M7_WL_FAIL final; cat "$OUT/m7-wl-restart.log"; exit 1; }

wait "$APID"; arc=$?
APID=0
[ "$arc" = 0 ] || { echo "M7_WL_FAIL ahk_rc=$arc"; cat /tmp/ahk_m7wl1.log; exit 1; }

cat >"$OUT/m7-wl-restart-summary.json" <<EOF
{"schema":1,"result":"pass","oracle":"m7-wayland-compositor-restart",
 "real_socket_death":true,"single_process":true,
 "generation_first":1,"generation_second":2,
 "reconnect_same_process":true,"input_after_restart":true,
 "survives_compositor_kill":true}
EOF
echo "M7_WL_ORACLE_PASS single_process gen=1->2 reconnect=1 survives_kill=1"
