#!/bin/bash
# ahk-inputd static multi-client arbitration oracle
# (check0901 P1-4 / check_detail0901 §7, milestone M4b).
#
# Independent target-output evidence + v2 decision/conflict sidebands:
#   1 OBSERVE: target receives original A
#   2 SUPPRESS: target receives nothing
#   3 A suppress + B observe + C remap: target receives one B sequence only
#   4 two remappers conflict; priority tie keeps first acceptance
#   5 PREEMPT_LOWER: higher priority displaces owner; target gets C only
#   6 owner crash releases lease; next owner acquires
#   7 lease expiry releases owner
#   8 key-up ownership: remap owner dies after down; replacement is neutralized
#     and original physical up remains suppressed
#   9 cross-UID EXCLUSIVE request is denied by capability policy
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
SUMMARY="$OUT/inputd-arbitration-summary.json"
WORK=/tmp/inputd-arb
KEY_A=30
KEY_B=48
KEY_C=46

sudo -n true 2>/dev/null || { echo "inputd arbitration oracle needs sudo -n"; exit 1; }
for p in $(pgrep -x ahk-inputd); do sudo -n kill -9 "$p" 2>/dev/null || true; done
rm -rf "$WORK"; mkdir -p "$WORK"
PASS=0; FAIL=0; FAILURES=""

PROBE="$WORK/inputd-v2-probe"
FIXTURE="$WORK/inputd-test-fixture"
OUTWATCH="$WORK/inputd-output-watch"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_v2_probe.c" -o "$PROBE"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_output_watch.c" -o "$OUTWATCH"

stop_running() {
  for pidf in "$WORK"/active-*.pid; do
    [ -f "$pidf" ] || continue
    sudo -n kill "$(cat "$pidf")" 2>/dev/null || true
  done
  sleep .15
  for pidf in "$WORK"/active-*.pid; do
    [ -f "$pidf" ] || continue
    sudo -n kill -9 "$(cat "$pidf")" 2>/dev/null || true
    rm -f "$pidf"
  done
  sudo -n pkill -9 -f inputd-v2-probe 2>/dev/null || true
  sudo -n pkill -9 -f inputd-output-watch 2>/dev/null || true
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
  sleep .4
}
cleanup() { stop_running; }
trap cleanup EXIT HUP INT TERM

expect() { # name expected actual
  local name=$1 expected=$2 actual=$3
  if [ "$actual" = "$expected" ]; then
    PASS=$((PASS+1)); echo "PASS $name [$actual]"
  else
    FAIL=$((FAIL+1)); FAILURES="$FAILURES $name(got=[$actual])"
    echo "FAIL $name expected=[$expected] got=[$actual]"
  fi
}
expect_contains() { # name needle haystack
  local name=$1 needle=$2 haystack=$3
  case "$haystack" in
    *"$needle"*) PASS=$((PASS+1)); echo "PASS $name" ;;
    *) FAIL=$((FAIL+1)); FAILURES="$FAILURES $name";
       echo "FAIL $name missing [$needle]" ;;
  esac
}
expect_absent() { # name needle haystack
  local name=$1 needle=$2 haystack=$3
  case "$haystack" in
    *"$needle"*) FAIL=$((FAIL+1)); FAILURES="$FAILURES $name";
       echo "FAIL $name unexpectedly found [$needle]" ;;
    *) PASS=$((PASS+1)); echo "PASS $name" ;;
  esac
}

start_fixture() { # name mode args...
  local name=$1; shift
  local devfile="$WORK/$name.dev"
  sudo -n env AHK_FIXTURE_NAME="$name" AHK_FIXTURE_DEVPATH="$devfile" \
    "$FIXTURE" "$@" >"$WORK/$name-fixture.log" 2>&1 &
  echo $! >"$WORK/active-fixture.pid"
  for _ in $(seq 1 100); do [ -s "$devfile" ] && break; sleep .03; done
  local node=$(cat "$devfile")
  for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .05; done
  [ -e "$node" ] || { echo "fixture node missing: $node"; exit 1; }
  echo "$node"
}
start_broker() { # name filter
  local name=$1 filter=$2
  sudo -n rm -f "$WORK/$name.sock" "$WORK/$name.sock.lock"
  sudo -n env AHK_INPUTD_TEST_DEVICE="$filter" "$BIN" \
    --socket "$WORK/$name.sock" --socket-mode 0666 -v >"$WORK/$name-broker.log" 2>&1 &
  echo $! >"$WORK/active-broker.pid"
  for _ in $(seq 1 100); do [ -S "$WORK/$name.sock" ] && break; sleep .03; done
  [ -S "$WORK/$name.sock" ] || { cat "$WORK/$name-broker.log"; exit 1; }
}
wait_grabbed() { # name
  for _ in $(seq 1 120); do grep -q "grabbed" "$WORK/$1-broker.log" 2>/dev/null && return 0; sleep .05; done
  return 1
}
start_arb() { # log socket code mode reg priority lease replacement level policy stay
  local log=$1 sock=$2; shift 2
  sudo -n "$PROBE" "$sock" arb "$@" >"$WORK/$log.out" 2>&1 &
  echo $! >"$WORK/active-$log.pid"
  for _ in $(seq 1 100); do grep -q '^ARB_ACK ' "$WORK/$log.out" 2>/dev/null && return 0; sleep .03; done
  cat "$WORK/$log.out"; return 1
}
start_output() { # log count timeout
  local log=$1 count=$2 timeout=$3
  sudo -n "$OUTWATCH" --count "$count" --timeout-ms "$timeout" >"$WORK/$log.out" 2>&1 &
  echo $! >"$WORK/active-output.pid"
  for _ in $(seq 1 100); do grep -q '^OUTPUT_DEVICE ' "$WORK/$log.out" 2>/dev/null && return 0; sleep .03; done
  cat "$WORK/$log.out"; return 1
}
kill_probe_pattern() { # exact-ish cmdline fragment
  local pids
  pids=$(pgrep -f "$1" 2>/dev/null || true)
  [ -n "$pids" ] && sudo -n kill -9 $pids 2>/dev/null || true
}

# 1. OBSERVE never changes target delivery.
stop_running
trig="$WORK/observe.trigger"; rm -f "$trig"
node=$(start_fixture observe --seq-trigger 1 $KEY_A "$trig")
start_broker observe "$node"; wait_grabbed observe
start_arb observe-rule "$WORK/observe.sock" $KEY_A 0 11 0 5000 0 0 0 --stay 3000
start_output observe-target 2 3000
touch "$trig"; wait "$(cat "$WORK/active-output.pid")" 2>/dev/null || true
observe_target=$(cat "$WORK/observe-target.out")
expect_contains arb_observe_down "OUT code=30 value=1" "$observe_target"
expect_contains arb_observe_up "OUT code=30 value=0" "$observe_target"
sleep .2
expect_contains arb_observe_decision "action=0 reason=0" "$(cat "$WORK/observe-rule.out")"

# 2. SUPPRESS blocks both original phases.
stop_running
trig="$WORK/suppress.trigger"; rm -f "$trig"
node=$(start_fixture suppress --seq-trigger 1 $KEY_A "$trig")
start_broker suppress "$node"; wait_grabbed suppress
start_arb suppress-rule "$WORK/suppress.sock" $KEY_A 1 12 5 5000 0 0 0 --stay 2500
start_output suppress-target 1 1800
touch "$trig"; wait "$(cat "$WORK/active-output.pid")" 2>/dev/null || true
suppress_target=$(cat "$WORK/suppress-target.out")
expect_contains arb_suppress_zero "OUTPUT_END count=0" "$suppress_target"
expect_absent arb_suppress_no_a "OUT code=30" "$suppress_target"
sleep .2
expect_contains arb_suppress_decision "action=1 reason=2 winner=12" "$(cat "$WORK/suppress-rule.out")"

# 3. A suppress + B observe + C remap: REMAP wins; one B sequence only.
stop_running
trig="$WORK/abc.trigger"; rm -f "$trig"
node=$(start_fixture abc --seq-trigger 1 $KEY_A "$trig")
start_broker abc "$node"; wait_grabbed abc
start_arb abc-observe "$WORK/abc.sock" $KEY_A 0 21 0 5000 0 0 0 --stay 3500
start_arb abc-suppress "$WORK/abc.sock" $KEY_A 1 22 100 5000 0 0 0 --stay 3500
start_arb abc-remap "$WORK/abc.sock" $KEY_A 3 23 1 5000 $KEY_B 3 0 --stay 3500
start_output abc-target 2 3000
touch "$trig"; wait "$(cat "$WORK/active-output.pid")" 2>/dev/null || true
abc_target=$(cat "$WORK/abc-target.out")
expect_contains arb_abc_b_down "OUT code=48 value=1" "$abc_target"
expect_contains arb_abc_b_up "OUT code=48 value=0" "$abc_target"
expect_absent arb_abc_no_a "OUT code=30" "$abc_target"
sleep .2
expect_contains arb_abc_winner "action=2 reason=4 winner=23" "$(cat "$WORK/abc-observe.out")"

# 4. Two remappers: reject policy and equal-priority tie keep first acceptance.
stop_running
start_broker conflict "name:definitely-not-a-device"
start_arb conflict-owner "$WORK/conflict.sock" $KEY_A 3 100 10 8000 $KEY_B 2 0 --stay 5000
conflict_out=$(sudo -n "$PROBE" "$WORK/conflict.sock" arb $KEY_A 3 200 20 5000 $KEY_C 2 0 --stay 0 || true)
expect_contains arb_conflict_ack "ARB_ACK reg=200 status=3 owner=100" "$conflict_out"
expect_contains arb_conflict_sideband "CONFLICT requested=200 owner=100" "$conflict_out"
tie_out=$(sudo -n "$PROBE" "$WORK/conflict.sock" arb $KEY_A 3 201 10 5000 $KEY_C 2 1 --stay 0 || true)
expect_contains arb_tie_first_wins "ARB_ACK reg=201 status=3 owner=100" "$tie_out"

# 5. Higher priority PREEMPT_LOWER displaces owner; target gets C only.
stop_running
trig="$WORK/preempt.trigger"; rm -f "$trig"
node=$(start_fixture preempt --seq-trigger 1 $KEY_A "$trig")
start_broker preempt "$node"; wait_grabbed preempt
start_arb preempt-old "$WORK/preempt.sock" $KEY_A 3 300 10 7000 $KEY_B 2 0 --stay 4500
start_arb preempt-new "$WORK/preempt.sock" $KEY_A 3 301 20 7000 $KEY_C 2 1 --stay 3500
expect_contains arb_preempt_granted "ARB_ACK reg=301 status=0" "$(cat "$WORK/preempt-new.out")"
for _ in $(seq 1 60); do grep -q 'reason=1' "$WORK/preempt-old.out" 2>/dev/null && break; sleep .05; done
expect_contains arb_preempt_old_notified "reason=1" "$(cat "$WORK/preempt-old.out")"
start_output preempt-target 2 3000
touch "$trig"; wait "$(cat "$WORK/active-output.pid")" 2>/dev/null || true
preempt_target=$(cat "$WORK/preempt-target.out")
expect_contains arb_preempt_c_down "OUT code=46 value=1" "$preempt_target"
expect_contains arb_preempt_c_up "OUT code=46 value=0" "$preempt_target"
expect_absent arb_preempt_no_b "OUT code=48" "$preempt_target"
expect_absent arb_preempt_no_a "OUT code=30" "$preempt_target"

# 6. Owner crash releases ownership; next registration acquires.
stop_running
start_broker crash "name:definitely-not-a-device"
start_arb crash-owner "$WORK/crash.sock" $KEY_A 3 400 10 9000 $KEY_B 2 0 --stay 8000
kill_probe_pattern "arb 30 3 400"
sleep .5
crash_new=$(sudo -n "$PROBE" "$WORK/crash.sock" arb $KEY_A 3 401 1 3000 $KEY_C 2 0 --stay 0)
expect_contains arb_crash_released "ARB_ACK reg=401 status=0" "$crash_new"
expect_contains arb_crash_log "registration 400" "$(cat "$WORK/crash-broker.log")"

# 7. Lease expiry releases owner.
stop_running
start_broker lease "name:definitely-not-a-device"
start_arb lease-owner "$WORK/lease.sock" $KEY_A 3 500 10 700 $KEY_B 2 0 --stay 2500
sleep 1.2
lease_new=$(sudo -n "$PROBE" "$WORK/lease.sock" arb $KEY_A 3 501 1 3000 $KEY_C 2 0 --stay 0)
expect_contains arb_lease_released "ARB_ACK reg=501 status=0" "$lease_new"
expect_contains arb_lease_expired "status=7" "$(cat "$WORK/lease-owner.out")"

# 8. Owner dies after physical down: replacement is neutralized and original
# physical up remains suppressed (key-up sticky ownership).
stop_running
down_trig="$WORK/keyowner.down"; up_trig="$WORK/keyowner.up"; rm -f "$down_trig" "$up_trig"
node=$(start_fixture keyowner --split-trigger $KEY_A "$down_trig" "$up_trig")
start_broker keyowner "$node"; wait_grabbed keyowner
start_arb keyowner-observe "$WORK/keyowner.sock" $KEY_A 0 600 0 6000 0 0 0 --stay 5000
start_arb keyowner-remap "$WORK/keyowner.sock" $KEY_A 3 601 10 6000 $KEY_B 2 0 --stay 5000
start_output keyowner-target 3 3800
touch "$down_trig"
for _ in $(seq 1 100); do grep -q 'OUT code=48 value=1' "$WORK/keyowner-target.out" 2>/dev/null && break; sleep .03; done
kill_probe_pattern "arb 30 3 601"
sleep .4
touch "$up_trig"
wait "$(cat "$WORK/active-output.pid")" 2>/dev/null || true
key_target=$(cat "$WORK/keyowner-target.out")
expect_contains arb_keyowner_b_down "OUT code=48 value=1" "$key_target"
expect_contains arb_keyowner_b_up "OUT code=48 value=0" "$key_target"
expect_absent arb_keyowner_no_a "OUT code=30" "$key_target"
expect_contains arb_keyowner_sticky "reason=5" "$(cat "$WORK/keyowner-observe.out")"

# 9. Cross-UID exclusive request is denied before registration.
stop_running
start_broker crossuid "name:definitely-not-a-device"
cross_out=$(sudo -n -u nobody "$PROBE" "$WORK/crossuid.sock" arb $KEY_A 2 700 10 3000 0 0 0 --stay 0 || true)
expect_contains arb_cross_uid_denied "ARB_ACK reg=700 status=4" "$cross_out"
expect_contains arb_cross_uid_cap "denied=0xe" "$cross_out"

{
  echo "{"
  echo "  \"schema\": 1,"
  echo "  \"oracle\": \"inputd-arbitration\","
  echo "  \"pass\": $PASS,"
  echo "  \"fail\": $FAIL,"
  echo "  \"failures\": \"$FAILURES\""
  echo "}"
} > "$SUMMARY"
echo "inputd-arbitration: PASS=$PASS FAIL=$FAIL"
cat "$SUMMARY"
[ "$FAIL" = 0 ] || exit 1
exit 0
