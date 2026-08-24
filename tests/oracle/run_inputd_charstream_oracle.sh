#!/bin/bash
# M4-C2: broker character stream.  With a broker running and an X11 layout
# source, an ahk_core script's Hotstring fires from broker-distributed EV_KEY
# events (decode through the three-layer model).  Requires root for the broker.
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
SOCK=/tmp/ahk-inputd-cs.sock
CMD=/tmp/m4c2_cmd
rm -f "$SOCK" "$SOCK.lock" "$CMD" /tmp/m4c2_*.ready /tmp/m4c2_*.fire /tmp/m4c2_*.log
pkill -9 -f 'out/ahk-inputd' 2>/dev/null
pkill -9 -f 'uinput-inject' 2>/dev/null
sleep .4

$SUDO "$OUT/ahk-inputd" --socket "$SOCK" >/tmp/m4c2_broker.log 2>&1 &
BPID=$!
sleep .6
grep -q 'ready on' /tmp/m4c2_broker.log || { echo BROKER_START_FAIL; cat /tmp/m4c2_broker.log; exit 1; }

cat >/tmp/m4c2.ahk <<'EOF'
#Requires AutoHotkey v2.0
OnTrigger(*) {
    FileAppend("fire`n", "/tmp/m4c2.fire")
    ExitApp
}
Hotstring(":B0X*:pq", OnTrigger)
FileAppend("ready`n", "/tmp/m4c2.ready")
SetTimer(() => ExitApp(4), -10000)
EOF

"$OUT/uinput-inject" "$CMD" >/tmp/m4c2_inject.log 2>&1 &
IPID=$!
sleep .5
AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$SOCK" \
  xvfb-run -a "$BIN" /tmp/m4c2.ahk >/tmp/m4c2_ahk.log 2>&1 &
APID=$!
for _ in $(seq 1 600); do test -f /tmp/m4c2.ready && break; sleep .02; done
test -f /tmp/m4c2.ready || { echo AHK_READY_FAIL; cat /tmp/m4c2_ahk.log; exit 1; }
broker_seen=0
for _ in $(seq 1 100); do
  grep -q 'broker mode active' /tmp/m4c2_ahk.log && { broker_seen=1; break; }
  sleep .05
done
[ "$broker_seen" = 1 ] || { echo NOT_BROKER_MODE; cat /tmp/m4c2_ahk.log; exit 1; }

sleep 1.6
echo '25 tap' >"$CMD"; sleep .3   # KEY_P
echo '16 tap' >"$CMD"; sleep .3   # KEY_Q
sleep 1.5

kill "$APID" "$IPID" 2>/dev/null
wait "$APID" 2>/dev/null; wait "$IPID" 2>/dev/null
$SUDO kill -9 "$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
rm -f "$SOCK" "$SOCK.lock" "$CMD"

test "$(grep -c '^fire$' /tmp/m4c2.fire 2>/dev/null)" -eq 1 || { echo CHARSTREAM_FIRE_FAIL; cat /tmp/m4c2.fire /tmp/m4c2_ahk.log 2>/dev/null; exit 1; }
cat >"$OUT/inputd-charstream-summary.json" <<EOF
{"schema":1,"result":"pass","broker_mode":true,"hotstring_trigger":"pq","fired":true}
EOF
echo "INPUTD_CHARSTREAM_ORACLE_PASS hotstring=1"
