#!/bin/bash
# ahk-inputd replay-failure fault oracle (check0901 P0-1 / check_detail0901 §1).
#
# External oracle proving the broker fails OPEN when the replay lane is
# unavailable or fails at runtime:
#   A. /dev/uinput cannot be opened (EACCES)        -> listen-only, NO grabs.
#   B. uinput device creation fails (ENOTTY)        -> listen-only, NO grabs.
#   C. runtime replay write failure                 -> all grabs released,
#        clients receive INPUTD_S2C_BACKEND_DEGRADED, physical keys flow.
#   D. held key at grab boundary                    -> grab deferred, retried
#        after release (two-phase EVIOCGKEY checks).
#
# Requires root (uinput + EVIOCGRAB).  The fixture keyboard keeps the host
# keyboard untouched (AHK_INPUTD_TEST_DEVICE limits grabs to the fixture).
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
SUMMARY="$OUT/inputd-replay-failure-summary.json"
WORK=/tmp/inputd-fault
KEY_A=30

command -v python3 >/dev/null || { echo "python3 missing"; exit 1; }
sudo -n true 2>/dev/null || { echo "inputd fault oracle needs root (sudo -n)"; exit 1; }
# Kill stale brokers from earlier runs (exact binary name match only).
for p in $(pgrep -x ahk-inputd); do sudo -n kill -9 "$p" 2>/dev/null; done
sleep .2

rm -rf "$WORK"; mkdir -p "$WORK"
PASS=0; FAIL=0; FAILURES=""

FIXTURE="$WORK/inputd-test-fixture"
PROBE="$WORK/inputd-grab-probe"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE" || exit 1
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_grab_probe.c" -o "$PROBE" || exit 1

cleanup() {
  for pidf in "$WORK"/*.pid; do
    [ -f "$pidf" ] || continue
    sudo -n kill "$(cat "$pidf")" 2>/dev/null || true
  done
  sleep .2
  for pidf in "$WORK"/*.pid; do
    [ -f "$pidf" ] || continue
    sudo -n kill -9 "$(cat "$pidf")" 2>/dev/null || true
  done
}
trap cleanup EXIT HUP INT TERM

probe() { # returns 0 when grabbable
  sudo -n "$PROBE" "$1"
}

start_daemon() { # name socket_suffix [env knobs...]
  local name=$1 suffix=$2; shift 2
  sudo -n env "$@" "$BIN" --socket "$WORK/$name$suffix.sock" --socket-mode 0666 -v \
    >"$WORK/$name.log" 2>&1 &
  echo $! >"$WORK/$name.pid"
}

wait_for() { # seconds condition-file
  local secs=$1 file=$2
  for _ in $(seq 1 "$((secs * 20))"); do [ -f "$file" ] && return 0; sleep .05; done
  return 1
}

start_fixture() { # name mode args...
  local name=$1; shift
  if [ -n "${PREV_FIX_PID:-}" ]; then
    sudo -n kill "$PREV_FIX_PID" 2>/dev/null || true
    sleep .2
  fi
  local devfile="$WORK/$name.devh"
  sudo -n env AHK_FIXTURE_NAME="$name" AHK_FIXTURE_DEVPATH="$devfile" \
    "$FIXTURE" "$@" >"$WORK/$name-fixture.log" 2>&1 &
  PREV_FIX_PID=$!
  echo $PREV_FIX_PID >"$WORK/$name-fixture.pid"
  for _ in $(seq 1 100); do [ -s "$devfile" ] && break; sleep .02; done
  local node=$(cat "$devfile")
  # Wait until udev has actually created the device node.
  for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .05; done
  [ -e "$node" ] || { echo "fixture node $node never appeared"; exit 1; }
  echo "$node"
}

expect() { # name expected actual
  local name=$1 expected=$2 actual=$3
  if [ "$actual" = "$expected" ]; then
    PASS=$((PASS+1)); echo "PASS $name [$actual]"
  else
    FAIL=$((FAIL+1)); FAILURES="$FAILURES $name(got=[$actual])"
    echo "FAIL $name expected=[$expected] got=[$actual]"
  fi
}

# ---- Case A: /dev/uinput open fails (EACCES) -> no grabs -------------------
node=$(start_fixture caseA --hold $KEY_A)
sudo -n rm -f "$WORK/caseA.sock" "$WORK/caseA.sock.lock"
start_daemon caseA a \
  AHK_INPUTD_TEST_UINPUT_PATH=/proc/1/mem \
  AHK_INPUTD_TEST_DEVICE="$node"
sleep .8
if grep -q "STATUS=degraded" "$WORK/caseA.log"; then
  expect caseA_degraded_status degraded "degraded"
else
  expect caseA_degraded_status degraded "missing"
fi
if sudo -n "$PROBE" "$node" >/dev/null 2>&1; then
  expect caseA_no_grab GRAB_AVAILABLE GRAB_AVAILABLE
else
  expect caseA_no_grab GRAB_AVAILABLE "GRAB_HELD"
fi

# ---- Case B: uinput creation fails (ENOTTY on /dev/null) -> no grabs -------
node=$(start_fixture caseB --hold $KEY_A)
sudo -n rm -f "$WORK/caseB.sock" "$WORK/caseB.sock.lock"
start_daemon caseB b \
  AHK_INPUTD_TEST_UINPUT_PATH=/dev/null \
  AHK_INPUTD_TEST_DEVICE="$node"
sleep .8
if grep -q "STATUS=degraded" "$WORK/caseB.log"; then
  expect caseB_degraded_status degraded "degraded"
else
  expect caseB_degraded_status degraded "missing"
fi
if sudo -n "$PROBE" "$node" >/dev/null 2>&1; then
  expect caseB_no_grab GRAB_AVAILABLE GRAB_AVAILABLE
else
  expect caseB_no_grab GRAB_AVAILABLE "GRAB_HELD"
fi

# ---- Case C: runtime replay failure -> releases grabs + DEGRADED -----------
node=$(start_fixture caseC --seq-trigger 4 $KEY_A "$WORK/caseC-trigger")
sudo -n rm -f "$WORK/caseC.sock" "$WORK/caseC.sock.lock"
start_daemon caseC c \
  AHK_INPUTD_TEST_DEVICE="$node" \
  AHK_INPUTD_TEST_REPLAY_FAIL_AFTER=2
# wait until the daemon actually grabs the fixture (udev/node creation can
# lag; the fixture delays its first tap by 6s to give the broker time)
grabbed=0
for _ in $(seq 1 150); do
  if ! sudo -n "$PROBE" "$node" >/dev/null 2>&1; then grabbed=1; break; fi
  sleep .05
done
expect caseC_initial_grab GRAB_HELD "$([ $grabbed = 1 ] && echo GRAB_HELD || echo GRAB_AVAILABLE)"
# client subscribes KEY_A; the fixture taps 4 times after its 1.2s delay;
# the second replay batch must fail and trigger fail-open.
python3 - "$WORK/caseCc.sock" >"$WORK/caseC-client.json" <<'PY' &
import socket, struct, sys, time, json
path = sys.argv[1]
for _ in range(100):
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(path)
        break
    except OSError:
        time.sleep(0.02)
else:
    print(json.dumps({"events": -1, "degraded": False})); sys.exit(0)
def frame(payload):
    s.sendall(struct.pack('<I', len(payload)) + payload)
frame(bytes([1]) + struct.pack('<I', 1))                       # HELLO v1
frame(bytes([2]) + struct.pack('<I', 1) + struct.pack('<I', 30) + bytes([0]))  # SUBSCRIBE KEY_A observe
events = 0
degraded = False
s.settimeout(10)
end = time.time() + 10
try:
    while time.time() < end:
        try:
            t = s.recv(1)
            if not t:
                break
        except socket.timeout:
            break
        if t[0] == 1:      # EVENT
            s.recv(13); events += 1
        elif t[0] == 2:    # ACK
            s.recv(5)
        elif t[0] == 3:    # PONG
            pass
        elif t[0] == 4:    # BACKEND_DEGRADED
            degraded = True
        else:
            break
except OSError:
    pass
print(json.dumps({"events": events, "degraded": degraded}))
PY
client_pid=$!
# Give the client time to connect and subscribe, then trigger the taps.
sleep 1.5
: >"$WORK/caseC-trigger"
wait $client_pid
client=$(cat "$WORK/caseC-client.json")
events=$(python3 -c "import json;print(json.loads(open('$WORK/caseC-client.json').read())['events'])")
degraded=$(python3 -c "import json;print(json.loads(open('$WORK/caseC-client.json').read())['degraded'])")
expect caseC_client_degraded True "$degraded"
events_ok=$([ "$events" -ge 1 ] && echo yes || echo no)
expect caseC_client_saw_events yes "$events_ok"
if grep -q "STATUS=degraded" "$WORK/caseC.log"; then
  expect caseC_degraded_status degraded "degraded"
else
  expect caseC_degraded_status degraded "missing"
fi
if sudo -n "$PROBE" "$node" >/dev/null 2>&1; then
  expect caseC_grabs_released GRAB_AVAILABLE GRAB_AVAILABLE
else
  expect caseC_grabs_released GRAB_AVAILABLE "GRAB_HELD"
fi

# ---- Case D: held key at grab boundary -> defer, retry after release ------
node=$(start_fixture caseD --release-after 1500 $KEY_A)
sudo -n rm -f "$WORK/caseD.sock" "$WORK/caseD.sock.lock"
start_daemon caseD d AHK_INPUTD_TEST_DEVICE="$node"
sleep .8
if grep -q "grab deferred" "$WORK/caseD.log"; then
  expect caseD_defer_log deferred "deferred"
else
  expect caseD_defer_log deferred "missing"
fi
if sudo -n "$PROBE" "$node" >/dev/null 2>&1; then
  expect caseD_not_grabbed_while_held GRAB_AVAILABLE GRAB_AVAILABLE
else
  expect caseD_not_grabbed_while_held GRAB_AVAILABLE "GRAB_HELD"
fi
# The fixture releases the held key after 1.5s and stays alive; the daemon's
# periodic rescan must then commit the grab (two-phase retry).
sleep 2.5
if ! sudo -n "$PROBE" "$node" >/dev/null 2>&1; then
  expect caseD_grabbed_after_release GRAB_HELD GRAB_HELD
else
  expect caseD_grabbed_after_release GRAB_HELD "GRAB_AVAILABLE"
fi

if [ "$FAIL" = 0 ]; then
  cat >"$SUMMARY" <<EOF
{"schema":1,"result":"pass","pass":$PASS,"fail":0,"scope":"inputd-replay-fail-open"}
EOF
else
  cat >"$SUMMARY" <<EOF
{"schema":1,"result":"fail","pass":$PASS,"fail":$FAIL,"failures":"$FAILURES"}
EOF
fi
echo "INPUTD_REPLAY_FAILURE_ORACLE_RESULT pass=$PASS fail=$FAIL summary=$SUMMARY"
[ "$FAIL" = 0 ]
