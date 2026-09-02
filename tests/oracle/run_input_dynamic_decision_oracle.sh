#!/bin/bash
# M5b-4 dynamic broker decision deadline/fail-open oracle.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
AHK="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BROKER" in /*) ;; *) BROKER="$ROOT/$BROKER" ;; esac
case "$AHK" in /*) ;; *) AHK="$ROOT/$AHK" ;; esac
OUT="$ROOT/tests/oracle/out"; mkdir -p "$OUT"
WORK=/tmp/input-dynamic; rm -rf "$WORK"; mkdir -p "$WORK"
PROBE="$WORK/inputd-v2-probe"; FIXTURE="$WORK/inputd-test-fixture"; WATCH="$WORK/inputd-output-watch"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_v2_probe.c" -o "$PROBE"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_output_watch.c" -o "$WATCH"
sudo -n true 2>/dev/null || { echo "dynamic decision oracle needs sudo -n"; exit 1; }
HOTIF_PID=""
cleanup() {
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
  sudo -n pkill -9 -f "^$FIXTURE " 2>/dev/null || true
  sudo -n pkill -9 -f "^$PROBE " 2>/dev/null || true
  sudo -n pkill -9 -f "^$WATCH " 2>/dev/null || true
  sudo -n pkill -9 -f "^$AHK $WORK/hotif.ahk " 2>/dev/null || true
  sudo -n pkill -9 -f '^Xvfb :91 ' 2>/dev/null || true
  sudo -n rm -f /tmp/.X91-lock /tmp/.X11-unix/X91 2>/dev/null || true
  [ -n "$HOTIF_PID" ] && sudo -n kill -9 "$HOTIF_PID" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
PASS=0; FAIL=0; FAILURES=""
expect_contains(){ local n=$1 q=$2 h=$3; case "$h" in *"$q"*) PASS=$((PASS+1)); echo "PASS $n";; *) FAIL=$((FAIL+1)); FAILURES="$FAILURES $n"; echo "FAIL $n missing [$q]";; esac; }
expect_absent(){ local n=$1 q=$2 h=$3; case "$h" in *"$q"*) FAIL=$((FAIL+1)); FAILURES="$FAILURES $n"; echo "FAIL $n found [$q]";; *) PASS=$((PASS+1)); echo "PASS $n";; esac; }
expect(){ local n=$1 e=$2 a=$3; if [ "$e" = "$a" ]; then PASS=$((PASS+1)); echo "PASS $n [$a]"; else FAIL=$((FAIL+1)); FAILURES="$FAILURES $n"; echo "FAIL $n expected=$e got=$a"; fi; }

run_case(){ # name taps decision-args output-count
  local name=$1 taps=$2 output_count=$3; shift 3
  cleanup; sleep .25
  local trig="$WORK/$name.trigger" dev="$WORK/$name.dev" sock="$WORK/$name.sock"
  rm -f "$trig" "$dev" "$sock" "$sock.lock"
  sudo -n env AHK_FIXTURE_NAME="$name-$$" AHK_FIXTURE_DEVPATH="$dev" \
    "$FIXTURE" --seq-trigger "$taps" 30 "$trig" >"$WORK/$name-fixture.log" 2>&1 &
  for _ in $(seq 1 100); do [ -s "$dev" ] && break; sleep .03; done
  local node=$(cat "$dev"); for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .03; done
  sudo -n env AHK_INPUTD_TEST_DEVICE="$node" "$BROKER" --socket "$sock" --socket-mode 0666 -v >"$WORK/$name-broker.log" 2>&1 &
  for _ in $(seq 1 120); do grep -q grabbed "$WORK/$name-broker.log" 2>/dev/null && break; sleep .03; done
  sudo -n "$PROBE" "$sock" arb 30 1 900 10 8000 0 0 0 --dynamic "$@" --stay 5000 >"$WORK/$name-client.log" 2>&1 &
  for _ in $(seq 1 120); do grep -q 'ARB_ACK reg=900 status=0' "$WORK/$name-client.log" 2>/dev/null && break; sleep .03; done
  grep -q 'ARB_ACK reg=900 status=0' "$WORK/$name-client.log"
  set +e
  sudo -n "$WATCH" --count "$output_count" --timeout-ms 3500 >"$WORK/$name-target.log" 2>&1 & WP=$!
  set -e
  for _ in $(seq 1 100); do grep -q OUTPUT_DEVICE "$WORK/$name-target.log" 2>/dev/null && break; sleep .03; done
  start_ms=$(date +%s%3N)
  touch "$trig"
  if [ "$output_count" -ge 2 ]; then
    for _ in $(seq 1 100); do grep -q 'OUT code=30 value=1' "$WORK/$name-target.log" 2>/dev/null && break; sleep .02; done
    end_ms=$(date +%s%3N)
    echo $((end_ms - start_ms)) >"$WORK/$name-elapsed-ms"
  fi
  sleep 2
  wait "$WP" 2>/dev/null || true
}

run_case pass 1 2 --decision pass
expect_contains dynamic_pass_request 'DECISION_REQUEST' "$(cat "$WORK/pass-client.log")"
expect_contains dynamic_pass_reply 'action=pass' "$(cat "$WORK/pass-client.log")"
expect_contains dynamic_pass_target_down 'OUT code=30 value=1' "$(cat "$WORK/pass-target.log")"
expect_contains dynamic_pass_target_up 'OUT code=30 value=0' "$(cat "$WORK/pass-target.log")"
expect_contains dynamic_pass_reason 'action=0 reason=7 winner=900' "$(cat "$WORK/pass-client.log")"

run_case suppress 1 1 --decision suppress
expect_contains dynamic_suppress_reply 'action=suppress' "$(cat "$WORK/suppress-client.log")"
expect_contains dynamic_suppress_zero 'OUTPUT_END count=0' "$(cat "$WORK/suppress-target.log")"
expect_absent dynamic_suppress_no_original 'OUT code=30' "$(cat "$WORK/suppress-target.log")"
expect_contains dynamic_suppress_reason 'action=1 reason=6 winner=900' "$(cat "$WORK/suppress-client.log")"

run_case timeout 1 2 --no-reply
expect_contains dynamic_timeout_request 'DECISION_REQUEST' "$(cat "$WORK/timeout-client.log")"
expect_contains dynamic_timeout_target 'OUT code=30 value=1' "$(cat "$WORK/timeout-target.log")"
expect_contains dynamic_timeout_reason 'action=0 reason=8 winner=900' "$(cat "$WORK/timeout-client.log")"
timeout_elapsed=$(cat "$WORK/timeout-elapsed-ms")
if [ "$timeout_elapsed" -le 500 ]; then PASS=$((PASS+1)); echo "PASS dynamic_timeout_bound [$timeout_elapsed ms]"; else FAIL=$((FAIL+1)); FAILURES="$FAILURES dynamic_timeout_bound"; echo "FAIL dynamic_timeout_bound [$timeout_elapsed ms]"; fi

run_case crash 1 2 --crash-on-request
expect_contains dynamic_crash_request 'DECISION_REQUEST' "$(cat "$WORK/crash-client.log")"
expect_contains dynamic_crash_target 'OUT code=30 value=1' "$(cat "$WORK/crash-target.log")"
expect_contains dynamic_crash_reason 'owner disconnected; fail-open' "$(cat "$WORK/crash-broker.log")"

run_case slow 4 8 --no-reply
requests=$(grep -c '^DECISION_REQUEST ' "$WORK/slow-client.log" || true)
expect dynamic_slow_three_requests 3 "$requests"
expect_contains dynamic_slow_conflict 'reason=5' "$(cat "$WORK/slow-client.log")"
expect_contains dynamic_slow_log 'downgraded observe-only after 3 timeout' "$(cat "$WORK/slow-broker.log")"
expect_contains dynamic_slow_target 'OUTPUT_END count=8' "$(cat "$WORK/slow-target.log")"
expect_contains dynamic_slow_reason 'reason=10' "$(cat "$WORK/slow-client.log")"

# A false higher-priority dynamic candidate yields to the next static winner.
cleanup; sleep .25
FBTRIG="$WORK/fallback.trigger"; FBDEV="$WORK/fallback.dev"; FBSOCK="$WORK/fallback.sock"
rm -f "$FBTRIG" "$FBDEV" "$FBSOCK" "$FBSOCK.lock"
sudo -n env AHK_FIXTURE_NAME="dynamic-fallback-$$" AHK_FIXTURE_DEVPATH="$FBDEV" \
  "$FIXTURE" --seq-trigger 1 30 "$FBTRIG" >"$WORK/fallback-fixture.log" 2>&1 &
for _ in $(seq 1 100); do [ -s "$FBDEV" ] && break; sleep .03; done
node=$(cat "$FBDEV"); for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .03; done
sudo -n env AHK_INPUTD_TEST_DEVICE="$node" "$BROKER" --socket "$FBSOCK" --socket-mode 0666 -v >"$WORK/fallback-broker.log" 2>&1 &
for _ in $(seq 1 120); do grep -q grabbed "$WORK/fallback-broker.log" 2>/dev/null && break; sleep .03; done
sudo -n "$PROBE" "$FBSOCK" arb 30 1 901 10 8000 0 0 0 --stay 4500 >"$WORK/fallback-static.log" 2>&1 &
for _ in $(seq 1 120); do grep -q 'ARB_ACK reg=901 status=0' "$WORK/fallback-static.log" 2>/dev/null && break; sleep .03; done
sudo -n "$PROBE" "$FBSOCK" arb 30 1 900 20 8000 0 0 0 --dynamic --decision pass --stay 4500 >"$WORK/fallback-dynamic.log" 2>&1 &
for _ in $(seq 1 120); do grep -q 'ARB_ACK reg=900 status=0' "$WORK/fallback-dynamic.log" 2>/dev/null && break; sleep .03; done
set +e
sudo -n "$WATCH" --count 1 --timeout-ms 2500 >"$WORK/fallback-target.log" 2>&1 & WP=$!
set -e
for _ in $(seq 1 100); do grep -q OUTPUT_DEVICE "$WORK/fallback-target.log" 2>/dev/null && break; sleep .03; done
touch "$FBTRIG"; wait "$WP" 2>/dev/null || true
expect_contains dynamic_fallback_pass_reply 'action=pass' "$(cat "$WORK/fallback-dynamic.log")"
expect_contains dynamic_fallback_static_winner 'action=1 reason=2 winner=901' "$(cat "$WORK/fallback-dynamic.log")"
expect_contains dynamic_fallback_suppressed 'OUTPUT_END count=0' "$(cat "$WORK/fallback-target.log")"
expect_absent dynamic_fallback_no_original 'OUT code=30' "$(cat "$WORK/fallback-target.log")"

# Real ahk_core HotIf: false replays F7; true F8-up suppresses a balanced pair
# and fires once on release.
cleanup; sleep .25
FTRIG="$WORK/hotif-false.trigger"; TTRIG="$WORK/hotif-true.trigger"
ALLOW="$WORK/hotif.allow"; DEV="$WORK/hotif.dev"; SOCK="$WORK/hotif.sock"
rm -f "$FTRIG" "$TTRIG" "$ALLOW" "$DEV" "$SOCK" "$SOCK.lock"
sudo -n env AHK_FIXTURE_NAME="dynamic-hotif-$$" AHK_FIXTURE_DEVPATH="$DEV" \
  "$FIXTURE" --two-trigger 65 "$FTRIG" 66 "$TTRIG" >"$WORK/hotif-fixture.log" 2>&1 &
for _ in $(seq 1 100); do [ -s "$DEV" ] && break; sleep .03; done
node=$(cat "$DEV"); for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .03; done
sudo -n env AHK_INPUTD_TEST_DEVICE="$node" "$BROKER" --socket "$SOCK" --socket-mode 0666 -v >"$WORK/hotif-broker.log" 2>&1 &
for _ in $(seq 1 120); do grep -q grabbed "$WORK/hotif-broker.log" 2>/dev/null && break; sleep .03; done
cat >"$WORK/hotif.ahk" <<'EOF'
#Requires AutoHotkey v2.0
count := 0
allow_now := false
Criterion(ThisHotkey) {
    global allow_now
    return allow_now
}
EnableGate() {
    global allow_now
    allow_now := true
    FileAppend("true`n", A_Args[3])
}
OnF7(*) {
    global count
    count += 1
    FileAppend("fire=" count " level=" A_SendLevel "`n", A_Args[1])
    SetTimer(Finish, -500)
}
Finish() {
    global count
    ExitApp(count = 1 ? 0 : 8)
}
HotIf(Criterion)
Hotkey("F7", OnF7)
Hotkey("F8 up", OnF7)
HotIf()
FileAppend("ready`n", A_Args[2])
SetTimer(EnableGate, -1200)
SetTimer(() => ExitApp(9), -7000)
EOF
sudo -n env AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$SOCK" \
  AHK_INPUT_PIPELINE=active AHK_INPUT_PIPELINE_TRACE="$WORK/hotif.trace" \
  xvfb-run --server-num=91 "$AHK" "$WORK/hotif.ahk" "$WORK/hotif.out" \
  "$WORK/hotif.ready" "$ALLOW" >"$WORK/hotif-ahk.log" 2>&1 & AP=$!
HOTIF_PID=$AP
for _ in $(seq 1 250); do
  [ -f "$WORK/hotif.ready" ] && grep -q 'dynamic=1' "$WORK/hotif-broker.log" 2>/dev/null && break
  sleep .03
done
grep -q 'dynamic=1' "$WORK/hotif-broker.log"
set +e
sudo -n "$WATCH" --count 3 --timeout-ms 5000 >"$WORK/hotif-target.log" 2>&1 & WP=$!
set -e
for _ in $(seq 1 100); do grep -q OUTPUT_DEVICE "$WORK/hotif-target.log" 2>/dev/null && break; sleep .03; done
touch "$FTRIG"
for _ in $(seq 1 100); do grep -q 'OUT code=65 value=0' "$WORK/hotif-target.log" 2>/dev/null && break; sleep .03; done
for _ in $(seq 1 120); do [ -f "$ALLOW" ] && break; sleep .03; done
[ -f "$ALLOW" ]
touch "$TTRIG"
wait "$AP"; ahk_rc=$?
HOTIF_PID=""
wait "$WP" 2>/dev/null || true
expect hotif_ahk_rc 0 "$ahk_rc"
expect_contains hotif_one_callback 'fire=1 level=0' "$(cat "$WORK/hotif.out")"
expect_contains hotif_false_pass 'OUTPUT_END count=2' "$(cat "$WORK/hotif-target.log")"
expect_contains hotif_false_reason 'broker_action":"replay"' "$(cat "$WORK/hotif.trace")"
expect_contains hotif_true_reason 'broker_action":"suppress"' "$(cat "$WORK/hotif.trace")"
python3 - "$WORK/hotif.trace" <<'PY'
import json,sys
r=[json.loads(x) for x in open(sys.argv[1])]
b=[x for x in r if x.get('stage')=='broker_decision' and x.get('evdev_code')==65]
assert any(x.get('broker_reason_id')==7 and x.get('broker_action')=='replay' for x in b),b
u=[x for x in r if x.get('stage')=='broker_decision' and x.get('evdev_code')==66]
assert any(x.get('broker_reason_id')==6 and x.get('broker_action')=='suppress' for x in u),u
assert any(x.get('broker_reason_id')==5 and x.get('broker_action')=='suppress' for x in u),u
m=[x for x in r if x.get('stage')=='match' and x.get('evdev_code') in (65,66)]
assert sum(x.get('action','').startswith('trigger_') for x in m)==1,m
assert any(x.get('evdev_code')==66 and x.get('release') and x.get('action','').startswith('trigger_') for x in m),m
PY
PASS=$((PASS+1)); echo "PASS hotif_trace_true_false"

# X11 active/mirror/legacy must evaluate callback variants too (the pre-M5
# FindVariant path could never select them on Linux).
cat >"$WORK/hotif-x11.ahk" <<'EOF'
#Requires AutoHotkey v2.0
count := 0
allow_x11 := false
XCriterion(ThisHotkey) {
    global allow_x11
    return allow_x11
}
XEnable() {
    global allow_x11
    allow_x11 := true
    FileAppend("true`n", A_Args[3])
}
XFire(*) {
    global count
    count += 1
}
XFinish() {
    global count
    h := HotkeyBackendGet("~F9")
    FileAppend("fire=" count " mode=" h.pipeline_mode "`n", A_Args[1])
    ExitApp(count = 1 ? 0 : 7)
}
HotIf(XCriterion)
Hotkey("~F9", XFire)
HotIf()
FileAppend("ready`n", A_Args[2])
SetTimer(XEnable, -1300)
SetTimer(XFinish, -2800)
EOF
for mode in active mirror legacy; do
  rm -f "$WORK/x11-$mode.out" "$WORK/x11-$mode.ready" "$WORK/x11-$mode.allow" "$WORK/x11-$mode.trace"
  xvfb-run -a bash -c '
    mode=$1; ahk=$2; work=$3
    AHK_INPUT_PIPELINE="$mode" AHK_INPUT_PIPELINE_TRACE="$work/x11-$mode.trace" \
      "$ahk" "$work/hotif-x11.ahk" "$work/x11-$mode.out" \
      "$work/x11-$mode.ready" "$work/x11-$mode.allow" >"$work/x11-$mode.log" 2>&1 & ap=$!
    for i in $(seq 1 100); do [ -f "$work/x11-$mode.ready" ] && break; sleep .03; done
    xdotool key F9
    for i in $(seq 1 100); do [ -f "$work/x11-$mode.allow" ] && break; sleep .03; done
    xdotool key F9
    wait "$ap"
  ' _ "$mode" "$AHK" "$WORK"
  expect_contains "hotif_x11_${mode}_callback" "fire=1 mode=$mode" "$(cat "$WORK/x11-$mode.out")"
  expect_absent "hotif_x11_${mode}_mirror" '"equivalent":false' "$(cat "$WORK/x11-$mode.trace")"
done

cat >"$OUT/input-dynamic-decision-summary.json" <<EOF
{"schema":1,"result":"$([ "$FAIL" = 0 ] && echo pass || echo fail)","pass":$PASS,"fail":$FAIL,"failures":"$FAILURES","deadline_ms":60,"slow_limit":3,"timeout_policy":"fail-open"}
EOF
echo "input-dynamic-decision: PASS=$PASS FAIL=$FAIL"
cat "$OUT/input-dynamic-decision-summary.json"
[ "$FAIL" = 0 ]
