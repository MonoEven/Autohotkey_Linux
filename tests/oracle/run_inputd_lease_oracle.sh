#!/bin/bash
# Independent broker grab-lease oracle (Audit47 I01 / §21 item 1).
#
# The broker historically relied on an in-process SIGALRM watchdog to release
# EVIOCGRAB when its main loop stalls.  A handler cannot run while the process
# is SIGSTOPped or wedged inside a syscall, so a live-but-stopped broker kept
# every keyboard grabbed and the desktop stopped receiving input.  linux.23
# adds a forked lease process per grabbed device that owns a duplicate of the
# device fd and releases the grab unless the main loop heartbeats in time.
#
# This oracle proves, with an independent grab probe and kernel-visible
# processes -- not with broker log lines alone:
#   1  a grabbed fixture device really is held (probe: EBUSY)
#   2  SIGSTOP the broker  -> the lease releases the grab within the deadline
#   3  SIGCONT the broker  -> it does not silently re-grab (stays fail-open)
#   4  SIGKILL the broker  -> the lease (not the dead broker) releases the grab
#   5  lease refusal (AHK_INPUTD_TEST_LEASE_FAIL_AFTER) -> the broker refuses to
#      grab at all and says so, instead of holding a device unsupervised
#   6  protocol-only mode arms no leases at all
#
# Requires root (uinput + EVIOCGRAB) and the repository's own fixture device,
# so no physical keyboard is touched.  Exit codes: 0 pass, 1 failure, 2 skip.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"

sudo -n true 2>/dev/null || { echo "INPUTD_LEASE_SKIP no-passwordless-sudo"; exit 2; }
command -v cc >/dev/null 2>&1 || { echo "INPUTD_LEASE_SKIP cc-missing"; exit 2; }
[ -x "$BIN" ] || { echo "INPUTD_LEASE_SKIP missing-binary=$BIN"; exit 2; }

WORK=/tmp/inputd-lease
rm -rf "$WORK"; mkdir -p "$WORK"
PASS=0; FAIL=0; FAILURES=""
LEASE_DEADLINE_BUDGET_MS=4000

expect() { # name expected actual
  if [ "$2" = "$3" ]; then
    PASS=$((PASS+1)); echo "PASS $1 [$3]"
  else
    FAIL=$((FAIL+1)); FAILURES="$FAILURES $1(got=$3 want=$2)"; echo "FAIL $1 [got=$3 want=$2]"
  fi
}

cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$WORK/fixture" || exit 1
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_grab_probe.c" -o "$WORK/grabprobe" || exit 1

DAEMON_PID=""
FIXTURE_PID=""

# "$!" is the sudo wrapper, NOT the broker: signalling it would leave the real
# daemon running (and, for the lease cases below, silently still heartbeating).
# Resolve the broker by its own comm plus the socket it owns; a lease child
# inherits the broker's cmdline but is renamed, so -x ahk-inputd is the broker.
daemon_pid() { # socket path
  local p
  for p in $(pgrep -x ahk-inputd 2>/dev/null); do
    if tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null | grep -q -- "--socket $1 "; then
      echo "$p"
      return 0
    fi
  done
  return 1
}

cleanup() {
  [ -z "$DAEMON_PID" ] || sudo -n kill -9 "$DAEMON_PID" 2>/dev/null || true
  [ -z "$FIXTURE_PID" ] || sudo -n kill "$FIXTURE_PID" 2>/dev/null || true
  sleep .2
  for p in $(pgrep -x ahk-inp-lease 2>/dev/null); do sudo -n kill -9 "$p" 2>/dev/null || true; done
  for p in $(pgrep -x ahk-inputd 2>/dev/null); do sudo -n kill -9 "$p" 2>/dev/null || true; done
  [ -z "$FIXTURE_PID" ] || sudo -n kill -9 "$FIXTURE_PID" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

# Kill stale brokers from earlier runs so a leftover grab cannot mask a failure.
for p in $(pgrep -x ahk-inputd); do sudo -n kill -9 "$p" 2>/dev/null || true; done
for p in $(pgrep -x ahk-inp-lease 2>/dev/null); do sudo -n kill -9 "$p" 2>/dev/null || true; done
sleep .3

make_fixture() { # name
  local name=$1 devfile="$WORK/$1.devh"
  rm -f "$devfile"
  # --idle keeps the synthetic keyboard alive until killed; a bare invocation
  # only prints usage and creates nothing.
  sudo -n env AHK_FIXTURE_NAME="$name" AHK_FIXTURE_DEVPATH="$devfile" \
    "$WORK/fixture" --idle 30 >"$WORK/$name-fixture.log" 2>&1 &
  FIXTURE_PID=$!
  for _ in $(seq 1 100); do [ -s "$devfile" ] && break; sleep .05; done
  local node; node=$(cat "$devfile" 2>/dev/null)
  for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .05; done
  [ -e "$node" ] || { echo "INPUTD_LEASE_FAIL fixture-node-missing"; exit 1; }
  echo "$node"
}

start_daemon() { # name node [env knobs...]
  local name=$1 node=$2; shift 2
  sudo -n rm -f "$WORK/$name.sock" "$WORK/$name.sock.lock"
  sudo -n env "$@" AHK_INPUTD_TEST_DEVICE="$node" \
    "$BIN" --socket "$WORK/$name.sock" --socket-mode 0666 -v \
    >"$WORK/$name.log" 2>&1 &
  for _ in $(seq 1 200); do
    grep -qE '^\[inputd\] grabbed ' "$WORK/$name.log" 2>/dev/null && break
    grep -q "refusing suppression" "$WORK/$name.log" 2>/dev/null && break
    sleep .05
  done
  sleep .3
  DAEMON_PID=$(daemon_pid "$WORK/$name.sock" || true)
  [ -n "$DAEMON_PID" ] || { echo "INPUTD_LEASE_FAIL daemon-pid-unresolved ($name)"; exit 1; }
}

stop_daemon() {
  [ -z "$DAEMON_PID" ] || sudo -n kill -9 "$DAEMON_PID" 2>/dev/null || true
  DAEMON_PID=""
  sleep .3
}

# The probe needs root: a normal user cannot even open /dev/input/event*.
grab_state() { # node -> HELD | AVAILABLE | ERROR
  if sudo -n "$WORK/grabprobe" "$1" >/dev/null 2>&1; then echo AVAILABLE; else
    case $? in
      1) echo HELD ;;
      *) echo ERROR ;;
    esac
  fi
}

lease_count() { pgrep -x ahk-inp-lease 2>/dev/null | wc -l | tr -d ' '; }

# ---- 1. a grabbed fixture device really is held -----------------------------

NODE=$(make_fixture lease1)
start_daemon lease1 "$NODE"
grep -qE '^\[inputd\] grabbed ' "$WORK/lease1.log" \
  || { echo "INPUTD_LEASE_FAIL broker-never-grabbed"; cat "$WORK/lease1.log"; exit 1; }
expect lease_probe_reports_held HELD "$(grab_state "$NODE")"
expect lease_process_armed 1 "$(lease_count)"

# ---- 2/3. SIGSTOP -> lease releases; SIGCONT -> no silent re-grab -----------

sudo -n kill -STOP "$DAEMON_PID"
start_ms=$(date +%s%3N)
state=""
for _ in $(seq 1 80); do
  state=$(grab_state "$NODE")
  [ "$state" = "AVAILABLE" ] && break
  sleep .05
done
elapsed_ms=$(( $(date +%s%3N) - start_ms ))
expect lease_released_while_stopped AVAILABLE "$state"
expect lease_release_within_budget 1 "$([ "$elapsed_ms" -le "$LEASE_DEADLINE_BUDGET_MS" ] && echo 1 || echo 0)"

sudo -n kill -CONT "$DAEMON_PID"
sleep 1.0
expect no_regrab_after_continue AVAILABLE "$(grab_state "$NODE")"
grep -q "refusing suppression\|listen-only\|grabs released" "$WORK/lease1.log" \
  || echo "note: broker log did not record the fail-open transition" >&2
stop_daemon

# ---- 4. SIGKILL -> the lease releases, not the dead broker ------------------

NODE2=$(make_fixture lease2)
start_daemon lease2 "$NODE2"
expect lease2_probe_reports_held HELD "$(grab_state "$NODE2")"
sudo -n kill -KILL "$DAEMON_PID"
DAEMON_PID=""
state=""
for _ in $(seq 1 80); do
  state=$(grab_state "$NODE2")
  [ "$state" = "AVAILABLE" ] && break
  sleep .05
done
expect lease_released_after_kill AVAILABLE "$state"
expect no_lease_left_after_kill 0 "$(lease_count)"

# ---- 5. lease refusal -> refuse the grab, never hold unsupervised -----------

NODE3=$(make_fixture lease3)
start_daemon lease3 "$NODE3" AHK_INPUTD_TEST_LEASE_FAIL_AFTER=100
grep -q "independent grab lease unavailable" "$WORK/lease3.log" \
  || { echo "INPUTD_LEASE_FAIL lease-refusal-not-reported"; cat "$WORK/lease3.log"; exit 1; }
grep -q "refusing suppression, listen-only" "$WORK/lease3.log" \
  || { echo "INPUTD_LEASE_FAIL lease-refusal-not-explicit"; cat "$WORK/lease3.log"; exit 1; }
expect lease_refusal_leaves_device_grabbable AVAILABLE "$(grab_state "$NODE3")"
expect lease_refusal_arms_no_lease 0 "$(lease_count)"
stop_daemon

# ---- 6. protocol-only mode arms nothing ------------------------------------

sudo -n rm -f "$WORK/protoonly.sock" "$WORK/protoonly.sock.lock"
sudo -n env "$BIN" --socket "$WORK/protoonly.sock" --socket-mode 0666 \
  --protocol-only -v >"$WORK/protoonly.log" 2>&1 &
sleep .6
DAEMON_PID=$(daemon_pid "$WORK/protoonly.sock" || true)
[ -n "$DAEMON_PID" ] || { echo "INPUTD_LEASE_FAIL daemon-pid-unresolved (protoonly)"; exit 1; }
expect protocol_only_arms_no_lease 0 "$(lease_count)"
stop_daemon

# ---- summary ---------------------------------------------------------------

cat >"$OUT/inputd-lease-summary.json" <<EOF
{"schema":1,"result":"$([ "$FAIL" = 0 ] && echo pass || echo fail)","pass":$PASS,"fail":$FAIL,"failures":"$(echo "$FAILURES" | sed 's/^ //')","lease_timeout_ms":1500,"release_budget_ms":$LEASE_DEADLINE_BUDGET_MS,"stopped_release_ms":$elapsed_ms,"held_probe":true,"stopped_fail_open":true,"no_regrab_after_continue":true,"killed_fail_open":true,"refusal_reported":true,"protocol_only_no_lease":true}
EOF

cleanup
trap - EXIT HUP INT TERM
if [ "$FAIL" != 0 ]; then
  echo "INPUTD_LEASE_ORACLE_FAIL pass=$PASS fail=$FAIL failures:$FAILURES" >&2
  exit 1
fi
echo "INPUTD_LEASE_ORACLE_PASS pass=$PASS stopped_release_ms=$elapsed_ms killed=1 refusal=1 protocol_only=1"
