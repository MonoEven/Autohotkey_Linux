#!/bin/bash
# ahk-inputd broker oracle (M4-D): proves capture, multi-client distribution,
# suppression arbitration, fail-open on client crash and grab recovery after
# broker death. Requires root or a passwordless sudo for EVIOCGRAB.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
CC="${CC:-cc}"
$CC -O2 -Wall -Wextra -o "$OUT/ahk-inputd" "$ROOT/source/linux/inputd/inputd.c" || exit 2
$CC -O2 -Wall -Wextra -o "$OUT/inputd-client" "$ROOT/tests/oracle/inputd_client.c" || exit 2
$CC -O2 -Wall -Wextra -o "$OUT/inputd-watch" "$ROOT/tests/oracle/inputd_watch.c" || exit 2
$CC -O2 -Wall -Wextra -o "$OUT/uinput-inject" "$ROOT/tools/linux/uinput-inject.c" || exit 2

SUDO=""
[ "$(id -u)" = 0 ] || SUDO="${AHK_SUDO:-sudo}"
SOCK=/tmp/ahk-inputd-test.sock
LOCK=/tmp/ahk-inputd-test.sock.lock
CMD=/tmp/inputd_cmd
rm -f "$SOCK" "$LOCK" "$CMD" /tmp/inputd_client.out /tmp/inputd_watch.out
pkill -9 -f 'out/ahk-inputd' 2>/dev/null
pkill -9 -f 'inputd-client' 2>/dev/null
pkill -9 -f 'inputd-watch' 2>/dev/null
pkill -9 -f 'uinput-inject' 2>/dev/null
sleep .4

$SUDO "$OUT/ahk-inputd" --socket "$SOCK" -v >/tmp/inputd_broker.log 2>&1 &
BPID=$!
sleep .6
grep -q 'ready on' /tmp/inputd_broker.log || { echo "BROKER_START_FAIL"; cat /tmp/inputd_broker.log; exit 1; }

"$OUT/uinput-inject" "$CMD" >/tmp/inputd_inject.log 2>&1 &
IPID=$!
sleep .4
"$OUT/inputd-watch" >/tmp/inputd_watch.out 2>&1 &
WPID=$!
sleep .4
"$OUT/inputd-client" "$SOCK" "88:0,30:1" --timeout-ms 20000 >/tmp/inputd_client.out 2>&1 &
CPID=$!
sleep .6

# Wait for a rescan so the broker has grabbed the uinput keyboard before the
# first tap, then drive it through the command file.
sleep 1.6
echo '88 tap' >"$CMD"
sleep .5
echo '30 tap' >"$CMD"
sleep .8

grep -q 'ACK HELLO ok=1' /tmp/inputd_client.out || { echo "HELLO_FAIL"; cat /tmp/inputd_client.out; exit 1; }
grep -q 'ACK SUBSCRIBE ok=1 count=2' /tmp/inputd_client.out || { echo "SUBSCRIBE_FAIL"; cat /tmp/inputd_client.out; exit 1; }
grep -q 'EVENT code=88' /tmp/inputd_client.out || { echo "DISTRIBUTE_F12_FAIL"; cat /tmp/inputd_client.out; exit 1; }
grep -q 'EVENT code=30' /tmp/inputd_client.out || { echo "DISTRIBUTE_A_FAIL"; cat /tmp/inputd_client.out; exit 1; }
grep -q 'KEY code=88' /tmp/inputd_watch.out || { echo "REPLAY_F12_FAIL"; cat /tmp/inputd_watch.out; exit 1; }
if grep -q 'KEY code=30' /tmp/inputd_watch.out; then echo "SUPPRESS_A_FAIL"; cat /tmp/inputd_watch.out; exit 1; fi

# kill -9 the client: its suppression rule must vanish within ~1s (fail-open).
kill -9 "$CPID" 2>/dev/null; wait "$CPID" 2>/dev/null
sleep .6
echo '30 tap' >"$CMD"
sleep .8
grep -q 'KEY code=30' /tmp/inputd_watch.out || { echo "CLIENT_CRASH_CLEANUP_FAIL"; cat /tmp/inputd_watch.out; exit 1; }

# kill -9 the broker: keyboard must become grabbable again immediately.
kill -9 "$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
sleep .5
GRAB="$($SUDO "$OUT/inputd-watch" probe 2>/dev/null)"
[ "$GRAB" = "GRAB_OK" ] || { echo "BROKER_DEATH_RECOVERY_FAIL grab=$GRAB"; exit 1; }

kill "$IPID" "$WPID" 2>/dev/null; wait "$IPID" 2>/dev/null; wait "$WPID" 2>/dev/null
rm -f "$SOCK" "$LOCK" "$CMD"
cat >"$OUT/inputd-broker-summary.json" <<EOF
{"schema":1,"result":"pass","proto_version":1,"distributed":{"F12":true,"A":true},"replayed":{"F12":true},"suppressed":{"A":true},"client_crash_cleanup":true,"broker_death_recovery":true}
EOF
echo "INPUTD_BROKER_ORACLE_PASS summary=$OUT/inputd-broker-summary.json"
