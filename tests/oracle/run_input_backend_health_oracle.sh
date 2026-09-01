#!/bin/bash
# Unified backend health/generation/reconciliation oracle
# (check0901 P1-2 / check_detail0901 §5, milestone M2).
#
# Independent protocol evidence plus script-visible HotkeyBackendGet:
#   A protocol-only -> AVAILABLE (not false HEALTHY)
#   B replay setup failure -> DEGRADED, replay=false, errno visible
#   C healthy broker -> BINDING before SUBSCRIBE ACK, HEALTHY afterwards
#   D held key at restart -> RECONCILING_STATE until release/regrab
#   E same AHK PID survives manual broker SIGKILL/restart; local generation
#     increments, broker authority changes, registrations+held state reconcile
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
AHK="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BROKER" in /*) ;; *) BROKER="$ROOT/$BROKER" ;; esac
case "$AHK" in /*) ;; *) AHK="$ROOT/$AHK" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
SUMMARY="$OUT/input-backend-health-summary.json"
WORK=/tmp/input-health
KEY_A=30
sudo -n true 2>/dev/null || { echo "health oracle needs sudo -n"; exit 1; }
command -v xvfb-run >/dev/null || { echo "xvfb-run missing"; exit 1; }
for p in $(pgrep -x ahk-inputd); do sudo -n kill -9 "$p" 2>/dev/null || true; done
rm -rf "$WORK"; mkdir -p "$WORK"
PASS=0; FAIL=0; FAILURES=""
PROBE="$WORK/inputd-v2-probe"
FIXTURE="$WORK/inputd-test-fixture"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_v2_probe.c" -o "$PROBE"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE"

cleanup() {
  for pidf in "$WORK"/*.pid; do [ -f "$pidf" ] && sudo -n kill "$(cat "$pidf")" 2>/dev/null || true; done
  sleep .15
  for pidf in "$WORK"/*.pid; do [ -f "$pidf" ] && sudo -n kill -9 "$(cat "$pidf")" 2>/dev/null || true; done
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
  sudo -n pkill -9 -f "^$FIXTURE " 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
expect() { local n=$1 e=$2 a=$3; if [ "$e" = "$a" ]; then PASS=$((PASS+1)); echo "PASS $n [$a]"; else FAIL=$((FAIL+1)); FAILURES="$FAILURES $n(got=[$a])"; echo "FAIL $n expected=[$e] got=[$a]"; fi; }
contains() { case "$2" in *"$1"*) return 0;; *) return 1;; esac; }
expect_contains() { local n=$1 needle=$2 hay=$3; if contains "$needle" "$hay"; then PASS=$((PASS+1)); echo "PASS $n"; else FAIL=$((FAIL+1)); FAILURES="$FAILURES $n"; echo "FAIL $n missing [$needle]"; fi; }
stop_all() { cleanup; rm -f "$WORK"/*.pid; sleep .3; }
start_broker() { # name [env...] [--extra args]
  local name=$1; shift; local extra=""
  if [ "${1:-}" = "--extra" ]; then extra=$2; shift 2; fi
  sudo -n rm -f "$WORK/$name.sock" "$WORK/$name.sock.lock"
  # shellcheck disable=SC2086
  sudo -n env "$@" "$BROKER" --socket "$WORK/$name.sock" --socket-mode 0666 -v $extra >"$WORK/$name-broker.log" 2>&1 &
  echo $! >"$WORK/$name-broker.pid"
  for _ in $(seq 1 100); do [ -S "$WORK/$name.sock" ] && return 0; sleep .03; done
  cat "$WORK/$name-broker.log"; return 1
}
start_fixture() { # name mode args...
  local name=$1; shift; local devfile="$WORK/$name.dev"
  sudo -n env AHK_FIXTURE_NAME="$name-$$" AHK_FIXTURE_DEVPATH="$devfile" "$FIXTURE" "$@" >"$WORK/$name-fixture.log" 2>&1 &
  echo $! >"$WORK/$name-fixture.pid"
  for _ in $(seq 1 100); do [ -s "$devfile" ] && break; sleep .03; done
  local node=$(cat "$devfile")
  for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .05; done
  [ -e "$node" ] || return 1; echo "$node"
}

# A: protocol-only is AVAILABLE, not healthy/replay-capable.
stop_all; start_broker proto --extra "--protocol-only"
proto=$("$PROBE" "$WORK/proto.sock" health)
expect_contains health_proto_state "HEALTH state=2 permission=0" "$proto"
expect_contains health_proto_flags "flags=0xc" "$proto"
expect_contains health_proto_reason "protocol-only: output disabled" "$proto"

# B: replay setup failure is client-visible DEGRADED with errno/reason.
stop_all; start_broker degraded AHK_INPUTD_TEST_UINPUT_PATH=/dev/null
bad=$("$PROBE" "$WORK/degraded.sock" health)
expect_contains health_degraded_state "HEALTH state=5" "$bad"
expect_contains health_degraded_replay "flags=0x8" "$bad"
expect_contains health_degraded_reason "replay unavailable; grabs released" "$bad"
err=$(echo "$bad" | sed -nE 's/.* errno=([-0-9]+) .*/\1/p' | tail -1)
expect health_degraded_errno 1 "$([ "${err:-0}" -ne 0 ] && echo 1 || echo 0)"

# C: replay+device healthy; SUBSCRIBE ACK transitions BINDING -> HEALTHY.
stop_all
node=$(start_fixture healthy --idle)
start_broker healthy AHK_INPUTD_TEST_DEVICE="$node"
for _ in $(seq 1 100); do grep -q grabbed "$WORK/healthy-broker.log" && break; sleep .05; done
healthy=$("$PROBE" "$WORK/healthy.sock" health-sub)
expect_contains health_binding "HEALTH state=3 permission=1 flags=0xd" "$healthy"
expect_contains health_healthy "HEALTH state=4 permission=1 flags=0xf" "$healthy"
expect_contains health_coverage "devices=1 grabbed=1" "$healthy"
seq1=$(echo "$healthy" | sed -nE 's/.*health_seq=([0-9]+).*/\1/p' | head -1)
seq2=$(echo "$healthy" | sed -nE 's/.*health_seq=([0-9]+).*/\1/p' | tail -1)
expect health_seq_increases 1 "$([ "$seq2" -gt "$seq1" ] && echo 1 || echo 0)"

# D: held-key boundary remains RECONCILING until release and successful regrab.
stop_all
node=$(start_fixture held --release-after 1800 $KEY_A)
start_broker held AHK_INPUTD_TEST_DEVICE="$node"
"$PROBE" "$WORK/held.sock" watch "" --timeout-ms 5000 >"$WORK/held-watch.log" 2>&1 &
echo $! >"$WORK/held-watch.pid"
wait "$(cat "$WORK/held-watch.pid")" 2>/dev/null || true
held=$(cat "$WORK/held-watch.log")
expect_contains health_held_reconciling "HEALTH state=9" "$held"
expect_contains health_held_not_reconciled "flags=0xb" "$held"
expect_contains health_held_recovers "HEALTH state=4 permission=1 flags=0xf" "$held"
expect_contains health_held_grabbed "devices=1 grabbed=1" "$held"

# E: same AHK process reconnects to a new broker generation and exposes M2 API.
# Keep one idle fixture alive across the broker restart so HEALTHY means real
# capture coverage + replay + registration + held-state reconciliation.
stop_all
reconnect_node=$(start_fixture reconnect-device --idle)
start_broker reconnect AHK_INPUTD_TEST_DEVICE="$reconnect_node"
for _ in $(seq 1 100); do grep -q grabbed "$WORK/reconnect-broker.log" && break; sleep .05; done
cat >"$WORK/reconnect.ahk" <<'EOF'
#Requires AutoHotkey v2.0
#InputLevel 5
Noop(*) {
}
Hotkey("~F7", Noop)
firstGen := 0
firstAuthority := 0
CheckHealth() {
    global firstGen, firstAuthority
    h := HotkeyBackendGet("~F7")
    if (h.state != "healthy" || h.registrations_reconciled != 1 || h.held_state_reconciled != 1)
        return
    line := "pid=" DllCall("getpid", "Int") " generation=" h.generation " authority=" h.authority_generation " health_seq=" h.health_seq " broker_seq=" h.broker_health_seq " replay=" h.replay_available " state=" h.state " outcome=" h.compatibility_outcome " dispatch=" h.dispatch_semantic " provenance=" h.provenance_grade " level=" h.level_gate_grade " suppress=" h.suppression_grade " recovery=" h.recovery_grade " caps=" h.caps_granted "`n"
    if (firstGen = 0) {
        firstGen := h.generation
        firstAuthority := h.authority_generation
        FileAppend("first " line, A_Args[1])
    } else if (h.generation > firstGen && h.authority_generation != firstAuthority) {
        FileAppend("second " line, A_Args[1])
        ExitApp(0)
    }
}
FailHealth() {
    ExitApp(9)
}
SetTimer(CheckHealth, 100)
SetTimer(FailHealth, -15000)
EOF
rm -f "$WORK/reconnect.out"
( cd "$WORK" && AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$WORK/reconnect.sock" xvfb-run -a "$AHK" reconnect.ahk "$WORK/reconnect.out" >"$WORK/reconnect-ahk.log" 2>&1 ) &
echo $! >"$WORK/reconnect-ahk.pid"
for _ in $(seq 1 200); do grep -q '^first ' "$WORK/reconnect.out" 2>/dev/null && break; sleep .05; done
grep -q '^first ' "$WORK/reconnect.out"
old_wrapper=$(cat "$WORK/reconnect-broker.pid")
old_broker=$(pgrep -P "$old_wrapper" | head -1 || true)
[ -n "$old_broker" ] || old_broker=$old_wrapper
sudo -n kill -9 "$old_broker" 2>/dev/null || true
for _ in $(seq 1 100); do grep -q 'broker disconnected; retrying' "$WORK/reconnect-ahk.log" 2>/dev/null && break; sleep .03; done
grep -q 'broker disconnected; retrying' "$WORK/reconnect-ahk.log"
sleep .2
sudo -n rm -f "$WORK/reconnect.sock" "$WORK/reconnect.sock.lock"
start_broker reconnect AHK_INPUTD_TEST_DEVICE="$reconnect_node"
set +e
wait "$(cat "$WORK/reconnect-ahk.pid")"; ahk_rc=$?
set -e
expect health_reconnect_ahk_rc 0 "$ahk_rc"
first=$(grep '^first ' "$WORK/reconnect.out")
second=$(grep '^second ' "$WORK/reconnect.out")
pid1=$(echo "$first" | sed -E 's/.*pid=([0-9]+).*/\1/')
pid2=$(echo "$second" | sed -E 's/.*pid=([0-9]+).*/\1/')
gen1=$(echo "$first" | sed -E 's/.*generation=([0-9]+).*/\1/')
gen2=$(echo "$second" | sed -E 's/.*generation=([0-9]+).*/\1/')
auth1=$(echo "$first" | sed -E 's/.*authority=([-0-9]+).*/\1/')
auth2=$(echo "$second" | sed -E 's/.*authority=([-0-9]+).*/\1/')
expect health_same_ahk_pid "$pid1" "$pid2"
expect health_generation_increases 1 "$([ "$gen2" -gt "$gen1" ] && echo 1 || echo 0)"
expect health_authority_changes 1 "$([ "$auth1" != "$auth2" ] && echo 1 || echo 0)"
expect_contains health_api_supported "state=healthy outcome=supported dispatch=hook_like provenance=authoritative level=guaranteed" "$second"
expect_contains health_api_recovery "suppress=none recovery=generation_reconcile caps=1" "$second"
expect_contains health_disconnect_trace "broker disconnected; retrying for 5000ms" "$(cat "$WORK/reconnect-ahk.log")"
expect_contains health_reconnect_trace "broker reconnected" "$(cat "$WORK/reconnect-ahk.log")"

cat >"$SUMMARY" <<EOF
{"schema":1,"result":"$([ "$FAIL" = 0 ] && echo pass || echo fail)","pass":$PASS,"fail":$FAIL,"failures":"$FAILURES","same_ahk_pid":$pid1,"generation_before":$gen1,"generation_after":$gen2}
EOF
echo "input-backend-health: PASS=$PASS FAIL=$FAIL"
cat "$SUMMARY"
[ "$FAIL" = 0 ]
