#!/bin/bash
# ahk-inputd protocol v2 oracle (check0901 P0-3 / check_detail0901 §3, M3).
#
# External oracle proving the v2 wire protocol: negotiation, framing
# robustness, capability authorization, authoritative broker-lane provenance
# shared across A/B clients, v1/v2 coexistence, restart generation and
# fail-open degradation on the v2 channel.
#
# Cases:
#   1  v2 HELLO_ACK negotiation (proto/client_id/authority/generation/caps)
#   2  second client gets a distinct client_id, same authority+generation
#   3  same script nonce on a new connection -> still a fresh client_id
#   4  protocol range excludes v2 -> ERROR PROTO_UNSUPPORTED
#   5  duplicate HELLO -> ERROR DUPLICATE_HELLO
#   6  SUBSCRIBE before HELLO -> ERROR NOT_HELLOED
#   7  non-monotonic client_seq -> ERROR SEQUENCE_VIOLATION
#   8  bad magic -> connection closed
#   9  oversized message_len -> connection closed
#   10 unknown message type -> ERROR BAD_FRAME
#   11 v1 client coexists on the same socket (legacy ACK/EVENT frames)
#   12 v2 EVENT provenance: down/up of a fixture key arrive with
#      source=PHYSICAL confidence=AUTHORITATIVE level=-1, strictly
#      increasing event_seq, identical seq across two v2 clients (A/B)
#   13 SUPPRESS requested by a non-owner uid -> denied + ERROR on subscribe
#   14 broker restart -> fresh authority id (old incarnation invalid)
#   15 replay failure -> v2 client receives BACKEND_DEGRADED, grabs released
#   16 hot-added fixture device -> DEVICE_ADDED broadcast to a live client
#
# Requires root (uinput + EVIOCGRAB).  Fixtures keep the host keyboard
# untouched (AHK_INPUTD_TEST_DEVICE limits grabs).
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
SUMMARY="$OUT/inputd-v2-protocol-summary.json"
WORK=/tmp/inputd-v2
KEY_A=30

sudo -n true 2>/dev/null || { echo "inputd v2 oracle needs root (sudo -n)"; exit 1; }
# Kill stale brokers from earlier runs (exact binary name match only).
for p in $(pgrep -x ahk-inputd); do sudo -n kill -9 "$p" 2>/dev/null; done
sleep .2

rm -rf "$WORK"; mkdir -p "$WORK"
PASS=0; FAIL=0; FAILURES=""

FIXTURE="$WORK/inputd-test-fixture"
PROBE="$WORK/inputd-v2-probe"
V1CLIENT="$WORK/inputd-client"
GRABPROBE="$WORK/inputd-grab-probe"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE" || exit 1
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_v2_probe.c" -o "$PROBE" || exit 1
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_client.c" -o "$V1CLIENT" || exit 1
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_grab_probe.c" -o "$GRABPROBE" || exit 1

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

start_daemon() { # name socket_suffix [env knobs...] [--extra "broker args"]
  local name=$1 suffix=$2; shift 2
  local extra=""
  if [ "${1:-}" = "--extra" ]; then extra="$2"; shift 2; fi
  sudo -n rm -f "$WORK/$name$suffix.sock" "$WORK/$name$suffix.sock.lock"
  # shellcheck disable=SC2086
  sudo -n env "$@" "$BIN" --socket "$WORK/$name$suffix.sock" --socket-mode 0666 -v $extra \
    >"$WORK/$name.log" 2>&1 &
  echo $! >"$WORK/$name.pid"
}

start_fixture() { # name mode args...  (KILL_PREV=0 keeps the previous fixture alive)
  local name=$1; shift
  if [ -n "${PREV_FIX_PID:-}" ] && [ "${KILL_PREV:-1}" = "1" ]; then
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

wait_for_log() { # name regex seconds
  local name=$1 regex=$2 secs=$3
  for _ in $(seq 1 "$((secs * 20))"); do
    grep -q "$regex" "$WORK/$name.log" 2>/dev/null && return 0
    sleep .05
  done
  return 1
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

expect_contains() { # name needle haystack
  local name=$1 needle=$2 haystack=$3
  case "$haystack" in
    *"$needle"*)
      PASS=$((PASS+1)); echo "PASS $name" ;;
    *)
      FAIL=$((FAIL+1)); FAILURES="$FAILURES $name"
      echo "FAIL $name expected substring [$needle] missing" ;;
  esac
}

# ---- Cases 1-11: pure protocol (no devices needed) --------------------------
start_daemon proto a --extra "--protocol-only"
sleep .3

ack1=$("$PROBE" "$WORK/protoa.sock" hello)
case "$ack1" in
  HELLO_ACK\ *) : ;;
  *) echo "probe hello failed: $ack1"; exit 1 ;;
esac
echo "$ack1" | grep -Eq 'proto=2 client_id=1 authority=[0-9a-f]{32} generation=[0-9]+ caps=0x1 denied=0x0 flags=0x0' \
  && expect proto_hello_ack ok ok || expect proto_hello_ack ok "$ack1"

ack2=$("$PROBE" "$WORK/protoa.sock" hello)
case "$ack2" in
  HELLO_ACK\ *) : ;;
  *) echo "probe hello2 failed: $ack2"; exit 1 ;;
esac
expect proto_second_client_id 1 "$(echo "$ack2" | sed -E 's/.*client_id=([0-9]+).*/\1/' | { read c; [ "$c" -ge 2 ] && echo 1 || echo 0; })"
auth1=$(echo "$ack1" | sed -E 's/.*authority=([0-9a-f]+).*/\1/')
auth2=$(echo "$ack2" | sed -E 's/.*authority=([0-9a-f]+).*/\1/')
gen1=$(echo "$ack1" | sed -E 's/.*generation=([0-9]+).*/\1/')
gen2=$(echo "$ack2" | sed -E 's/.*generation=([0-9]+).*/\1/')
expect proto_shared_authority 1 "$([ "$auth1" = "$auth2" ] && [ "$gen1" = "$gen2" ] && echo 1 || echo 0)"

ack3=$("$PROBE" "$WORK/protoa.sock" hello --nonce 00112233445566778899aabbccddeeff)
expect proto_same_nonce_fresh_id 1 "$(echo "$ack3" | sed -E 's/.*client_id=([0-9]+).*/\1/' | { read c; [ "$c" -ge 3 ] && echo 1 || echo 0; })"

probe_out=$("$PROBE" "$WORK/protoa.sock" hello --proto 99 --expect-error)
expect_contains proto_range_error "ERROR code=1" "$probe_out"

probe_out=$("$PROBE" "$WORK/protoa.sock" hello2)
expect_contains proto_dup_hello "ERROR code=6" "$probe_out"

probe_out=$("$PROBE" "$WORK/protoa.sock" nothello)
expect_contains proto_not_helloed "ERROR code=3" "$probe_out"

probe_out=$("$PROBE" "$WORK/protoa.sock" seqviol)
expect_contains proto_seq_violation "ERROR code=5" "$probe_out"

probe_out=$("$PROBE" "$WORK/protoa.sock" badmagic)
expect_contains proto_bad_magic "CLOSED" "$probe_out"

probe_out=$("$PROBE" "$WORK/protoa.sock" oversized)
expect_contains proto_oversized "CLOSED" "$probe_out"

probe_out=$("$PROBE" "$WORK/protoa.sock" badtype)
expect_contains proto_bad_type "ERROR code=2" "$probe_out"

v1out=$("$V1CLIENT" "$WORK/protoa.sock" "30:0" --timeout-ms 1500)
expect_contains proto_v1_coexist_hello "ACK HELLO ok=1 proto=1" "$v1out"
expect_contains proto_v1_coexist_sub "ACK SUBSCRIBE ok=1 count=1" "$v1out"

# ---- Case 13: capability denial for a non-owner uid -------------------------
probe_out=$(sudo -n -u nobody "$PROBE" "$WORK/protoa.sock" deny-sup)
expect_contains proto_nobody_denied "denied=0x2" "$probe_out"
expect_contains proto_nobody_subscribe_denied "ERROR code=4" "$probe_out"

# ---- Case 14: restart -> fresh authority ------------------------------------
kill_broker() { # pidfile
  sudo -n kill -9 "$(cat "$1")" 2>/dev/null || true
  sleep .2
}
auth_before=$auth1
kill_broker "$WORK/proto.pid"
sudo -n rm -f "$WORK/protoa.sock" "$WORK/protoa.sock.lock"
start_daemon proto a --extra "--protocol-only"
sleep .3
ack4=$("$PROBE" "$WORK/protoa.sock" hello)
case "$ack4" in HELLO_ACK\ *) : ;; *) echo "probe hello (restart) failed: $ack4"; exit 1 ;; esac
auth_after=$(echo "$ack4" | sed -E 's/.*authority=([0-9a-f]+).*/\1/')
expect proto_restart_new_authority 1 "$([ "$auth_after" != "$auth_before" ] && echo 1 || echo 0)"
kill_broker "$WORK/proto.pid"

# ---- Cases 12/15/16: real fixture devices -----------------------------------
# Case 12: A/B shared authoritative provenance on broker-lane events.
node=$(start_fixture events --seq-trigger 2 $KEY_A "$WORK/events.trigger")
start_daemon events a AHK_INPUTD_TEST_DEVICE="$node"
wait_for_log events "grabbed" 5 || { echo "events broker never grabbed"; exit 1; }

"$PROBE" "$WORK/eventsa.sock" watch "30:0" --until-events 4 --timeout-ms 9000 >"$WORK/probeA.out" 2>&1 &
echo $! >"$WORK/probeA.pid"
"$PROBE" "$WORK/eventsa.sock" watch "30:0" --until-events 4 --timeout-ms 9000 >"$WORK/probeB.out" 2>&1 &
echo $! >"$WORK/probeB.pid"
"$V1CLIENT" "$WORK/eventsa.sock" "30:0" --timeout-ms 7000 >"$WORK/v1watch.out" 2>&1 &
echo $! >"$WORK/v1watch.pid"
sleep .6
touch "$WORK/events.trigger"
for _ in $(seq 1 200); do
  [ -s "$WORK/probeA.out" ] && grep -q "WATCH_END" "$WORK/probeA.out" && break
  sleep .05
done
for pidf in probeA.pid probeB.pid v1watch.pid; do
  sudo -n kill "$(cat "$WORK/$pidf")" 2>/dev/null || true
  wait "$(cat "$WORK/$pidf")" 2>/dev/null || true
done

event_lines_A=$(grep -c '^EVENT ' "$WORK/probeA.out" || true)
expect v2_events_four_A 4 "$event_lines_A"
expect_contains v2_events_physical "src=0 conf=0 level=-1" "$(grep '^EVENT ' "$WORK/probeA.out" | head -1)"
expect_contains v2_events_physical_owner "prod=0 parent=0 code=30" "$(grep '^EVENT ' "$WORK/probeA.out" | head -1)"
physical_txns=$(grep '^EVENT ' "$WORK/probeA.out" | sed -E 's/.* txn=([0-9]+) prod=.*/\1/' | head -4 | tr '\n' ' ')
set -- $physical_txns
physical_txn_ok=0
[ "$#" = 4 ] && [ "$1" != 0 ] && [ "$1" = "$2" ] && [ "$3" = "$4" ] \
  && [ "$1" != "$3" ] && physical_txn_ok=1
expect v2_physical_txn_pairs 1 "$physical_txn_ok"
seqs_A=$(grep '^EVENT ' "$WORK/probeA.out" | sed -E 's/EVENT seq=([0-9]+).*/\1/' | tr '\n' ' ')
seqs_B=$(grep '^EVENT ' "$WORK/probeB.out" | sed -E 's/EVENT seq=([0-9]+).*/\1/' | tr '\n' ' ')
expect v2_events_shared_seq 1 "$([ "$seqs_A" = "$seqs_B" ] && [ -n "$seqs_A" ] && echo 1 || echo 0)"
strict=1; prev=0
for s in $seqs_A; do [ "$s" -gt "$prev" ] || strict=0; prev=$s; done
expect v2_events_seq_strict 1 "$strict"
expect_contains v1_coexist_events "EVENT code=30 value=1" "$(cat "$WORK/v1watch.out")"

# Case 15: replay failure -> v2 BACKEND_DEGRADED + grabs released.
kill_broker "$WORK/events.pid"
sudo -n rm -f "$WORK/eventsa.sock" "$WORK/eventsa.sock.lock" "$WORK/events.trigger"
node=$(start_fixture degraded --seq-trigger 2 $KEY_A "$WORK/degraded.trigger")
start_daemon degraded a \
  AHK_INPUTD_TEST_DEVICE="$node" \
  AHK_INPUTD_TEST_REPLAY_FAIL_AFTER=3
wait_for_log degraded "grabbed" 5 || { echo "degraded broker never grabbed"; exit 1; }
"$PROBE" "$WORK/degradeda.sock" watch "30:0" --timeout-ms 9000 >"$WORK/degraded-watch.out" 2>&1 &
echo $! >"$WORK/degraded-watch.pid"
sleep .6
touch "$WORK/degraded.trigger"
for _ in $(seq 1 200); do
  grep -q "BACKEND_DEGRADED" "$WORK/degraded-watch.out" 2>/dev/null && break
  sleep .05
done
sudo -n kill "$(cat "$WORK/degraded-watch.pid")" 2>/dev/null || true
wait "$(cat "$WORK/degraded-watch.pid")" 2>/dev/null || true
expect_contains v2_degraded_frame "BACKEND_DEGRADED" "$(cat "$WORK/degraded-watch.out")"
event_lines=$(grep -c '^EVENT ' "$WORK/degraded-watch.out" || true)
[ "$event_lines" -ge 2 ] && expect v2_degraded_pre_events ok ok || expect v2_degraded_pre_events ok "only $event_lines"
if sudo -n "$GRABPROBE" "$node" >/dev/null 2>&1; then
  expect v2_degraded_grabs_released GRAB_AVAILABLE GRAB_AVAILABLE
else
  expect v2_degraded_grabs_released GRAB_AVAILABLE GRAB_HELD
fi
kill_broker "$WORK/degraded.pid"

# Case 16: hot-added fixture -> DEVICE_ADDED to a live v2 client.
sudo -n rm -f "$WORK/hota.sock" "$WORK/hota.sock.lock"
KILL_PREV=0 node1=$(start_fixture hot1 --idle)
start_daemon hot a AHK_INPUTD_TEST_DEVICE="name:hot1,name:hot2"
wait_for_log hot "grabbed" 5 || { echo "hot broker never grabbed fixture 1"; exit 1; }
"$PROBE" "$WORK/hota.sock" watch "" --timeout-ms 9000 >"$WORK/hot-watch.out" 2>&1 &
echo $! >"$WORK/hot-watch.pid"
sleep .4
KILL_PREV=0 node2=$(start_fixture hot2 --idle)
# broker rescans every second; the hot2 name was pre-authorized via
# "name:hot2", so the rescan grabs it and broadcasts DEVICE_ADDED.
sleep 3
sudo -n kill "$(cat "$WORK/hot-watch.pid")" 2>/dev/null || true
wait "$(cat "$WORK/hot-watch.pid")" 2>/dev/null || true
expect_contains v2_device_added "DEVICE_ADDED id=" "$(cat "$WORK/hot-watch.out")"

# ---- summary ----------------------------------------------------------------
{
  echo "{"
  echo "  \"schema\": 1,"
  echo "  \"oracle\": \"inputd-v2-protocol\","
  echo "  \"pass\": $PASS,"
  echo "  \"fail\": $FAIL,"
  echo "  \"failures\": \"$FAILURES\""
  echo "}"
} > "$SUMMARY"
echo "inputd-v2-protocol: PASS=$PASS FAIL=$FAIL"
cat "$SUMMARY"
[ "$FAIL" = 0 ] || exit 1
exit 0
