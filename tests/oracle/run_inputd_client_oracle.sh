#!/bin/bash
# M4-C: ahk_core connects to ahk-inputd in broker mode; two scripts share one
# EV_KEY stream and both fire custom combos without BadAccess.  Requires root.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
CC="${CC:-cc}"
$CC -O2 -Wall -Wextra -o "$OUT/ahk-inputd" "$ROOT/source/linux/inputd/inputd.c" || exit 2
$CC -O2 -Wall -Wextra -o "$OUT/uinput-inject" "$ROOT/tools/linux/uinput-inject.c" || exit 2

SUDO=""
[ "$(id -u)" = 0 ] || SUDO="${AHK_SUDO:-sudo}"
SOCK=/tmp/ahk-inputd-m4c.sock
CMD=/tmp/m4c_cmd
rm -f "$SOCK" "$SOCK.lock" "$CMD" /tmp/m4c_*.ready /tmp/m4c_*.fire /tmp/m4c_*.log
pkill -9 -f 'out/ahk-inputd' 2>/dev/null
pkill -9 -f 'uinput-inject' 2>/dev/null
pkill -9 -f 'ahk_core.*m4c_' 2>/dev/null
sleep .4

$SUDO "$OUT/ahk-inputd" --socket "$SOCK" >/tmp/m4c_broker.log 2>&1 &
BPID=$!
sleep .6
grep -q 'ready on' /tmp/m4c_broker.log || { echo BROKER_START_FAIL; cat /tmp/m4c_broker.log; exit 1; }

cat >/tmp/m4c_a.ahk <<'EOF'
#Requires AutoHotkey v2.0
count := 0
OnCombo(*) {
    global count
    count++
    FileAppend("fire=" count "`n", "/tmp/m4c_a.fire")
    ExitApp
}
Hotkey("a & b", OnCombo)
FileAppend("ready`n", "/tmp/m4c_a.ready")
SetTimer(() => ExitApp(4), -12000)
EOF
cat >/tmp/m4c_b.ahk <<'EOF'
#Requires AutoHotkey v2.0
count := 0
OnCombo(*) {
    global count
    count++
    FileAppend("fire=" count "`n", "/tmp/m4c_b.fire")
    ExitApp
}
Hotkey("c & d", OnCombo)
FileAppend("ready`n", "/tmp/m4c_b.ready")
SetTimer(() => ExitApp(4), -12000)
EOF

"$OUT/uinput-inject" "$CMD" >/tmp/m4c_inject.log 2>&1 &
IPID=$!
sleep .5
AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$SOCK" "$BIN" /tmp/m4c_a.ahk >/tmp/m4c_a.log 2>&1 &
APID=$!
AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$SOCK" "$BIN" /tmp/m4c_b.ahk >/tmp/m4c_b.log 2>&1 &
BPID2=$!
for _ in $(seq 1 600); do
  test -f /tmp/m4c_a.ready && test -f /tmp/m4c_b.ready && break
  sleep .02
done
test -f /tmp/m4c_a.ready || { echo "A_READY_FAIL"; cat /tmp/m4c_a.log; }
test -f /tmp/m4c_b.ready || { echo "B_READY_FAIL"; cat /tmp/m4c_b.log; }
grep -q 'broker mode active' /tmp/m4c_a.log || { echo "A_NOT_BROKER_MODE"; cat /tmp/m4c_a.log; exit 1; }
grep -q 'broker mode active' /tmp/m4c_b.log || { echo "B_NOT_BROKER_MODE"; cat /tmp/m4c_b.log; exit 1; }

# Wait a rescan so the broker grabbed the uinput keyboard, then drive both.
sleep 1.6
echo '30 down' >"$CMD"; sleep .25
echo '48 tap' >"$CMD"; sleep .25
echo '30 up' >"$CMD"; sleep .5
echo '46 down' >"$CMD"; sleep .25
echo '32 tap' >"$CMD"; sleep .25
echo '46 up' >"$CMD"
sleep 1

kill "$APID" "$BPID2" "$IPID" 2>/dev/null
wait "$APID" 2>/dev/null; wait "$BPID2" 2>/dev/null
wait "$IPID" 2>/dev/null
$SUDO kill -9 "$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
rm -f "$SOCK" "$SOCK.lock" "$CMD"

test "$(grep -c '^fire=1$' /tmp/m4c_a.fire 2>/dev/null)" -eq 1 || { echo "A_FIRE_FAIL"; cat /tmp/m4c_a.fire /tmp/m4c_a.log 2>/dev/null; exit 1; }
test "$(grep -c '^fire=1$' /tmp/m4c_b.fire 2>/dev/null)" -eq 1 || { echo "B_FIRE_FAIL"; cat /tmp/m4c_b.fire /tmp/m4c_b.log 2>/dev/null; exit 1; }

cat >"$OUT/inputd-client-summary.json" <<EOF
{"schema":1,"result":"pass","clients":2,"broker_mode":true,"combo_A_fired":true,"combo_B_fired":true,"badaccess":false}
EOF
echo "INPUTD_CLIENT_ORACLE_PASS clients=2 combos=2"
