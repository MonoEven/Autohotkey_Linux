#!/bin/bash
# ahk-inputd broker-owned injection oracle (check0901 P0-3 §3.3C, M4a).
#
# External oracle for the M4 injection transaction protocol:
#   1  full transaction: BEGIN/EVENT(down)/EVENT(up)/COMMIT -> per-stage ACKs
#   2  authoritative provenance delivery: a second client observes the
#      synthetic events with source=OTHER, send_level, transaction id,
#      producer id and strictly increasing event_seq
#   3  capability denial: non-owner uid requesting INJECT -> DENIED
#   4  consumer level gate (Windows-golden): ahk_core hotkey InputLevel 5
#      fires for an injected level 10, does NOT fire for level 5
#   5  commit balances a held key (down-only + COMMIT -> synthetic key-up)
#   6  owner crash after down -> broker auto-aborts and balances the key
#   7  transaction TTL expiry -> timed-out abort + balance
#   8  broker restart -> the old generation's transaction id is STALE
#   9  preflight event_count bound -> BAD_FRAME
#   10 total transaction quota (16) -> QUOTA
#   11 v1 client coexists and observes injected events (legacy frames)
#
# Requires root (uinput).  TEST_DEVICE points at a nonexistent device name so
# the broker grabs nothing and never touches the host keyboard; injection
# runs on the broker-owned output device alone.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
AHK="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
case "$AHK" in /*) ;; *) AHK="$ROOT/$AHK" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
SUMMARY="$OUT/inputd-injection-summary.json"
WORK=/tmp/inputd-inject
KEY_F7=65

sudo -n true 2>/dev/null || { echo "inputd injection oracle needs root (sudo -n)"; exit 1; }
command -v xvfb-run >/dev/null || { echo "xvfb-run missing"; exit 1; }
for p in $(pgrep -x ahk-inputd); do sudo -n kill -9 "$p" 2>/dev/null; done
sleep .2

rm -rf "$WORK"; mkdir -p "$WORK"
PASS=0; FAIL=0; FAILURES=""

PROBE="$WORK/inputd-v2-probe"
V1CLIENT="$WORK/inputd-client"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_v2_probe.c" -o "$PROBE" || exit 1
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_client.c" -o "$V1CLIENT" || exit 1

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
  # Any probe left behind (sudo may have been killed instead of the child).
  sudo -n pkill -9 -f inputd-v2-probe 2>/dev/null || true
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

kill_probe() { # cmdline pattern (kills the probe child, not the sudo wrapper)
  local pids
  pids=$(pgrep -f "$1" 2>/dev/null || true)
  [ -n "$pids" ] && sudo -n kill -9 $pids 2>/dev/null || true
}

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

start_daemon inj a AHK_INPUTD_TEST_DEVICE="name:definitely-not-a-device"
sleep .4

# ---- 1: full transaction ---------------------------------------------------
out=$(sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 3)
expect_contains inj_begin_ok "status=0" "$(echo "$out" | grep 'status=0' | head -1)"
expect_contains inj_events_ok "status=8" "$out"
expect_contains inj_commit_ok "status=1" "$out"

# ---- 2: provenance delivery to a second client ------------------------------
"$PROBE" "$WORK/inja.sock" watch "65:0" --until-events 2 --timeout-ms 8000 >"$WORK/obs.out" 2>&1 &
echo $! >"$WORK/obs.pid"
sleep .4
sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 3 --txn 81 >/dev/null
for _ in $(seq 1 100); do grep -q "WATCH_END events=2" "$WORK/obs.out" 2>/dev/null && break; sleep .05; done
sudo -n kill "$(cat "$WORK/obs.pid")" 2>/dev/null || true
wait "$(cat "$WORK/obs.pid")" 2>/dev/null || true
obs=$(cat "$WORK/obs.out")
expect_contains inj_obs_down "src=2 conf=0 level=3 txn=81" "$obs"
expect_contains inj_obs_down_prov "parent=0 code=65 phase=0 value=1" "$obs"
expect_contains inj_obs_up "phase=1 value=0" "$obs"
seqs=$(echo "$obs" | grep '^EVENT ' | sed -E 's/EVENT seq=([0-9]+).*/\1/' | tr '\n' ' ')
strict=1; prev=0
for s in $seqs; do [ "$s" -gt "$prev" ] || strict=0; prev=$s; done
expect inj_obs_seq_strict 1 "$strict"

# ---- 3: non-owner uid denied ------------------------------------------------
out=$(sudo -n -u nobody "$PROBE" "$WORK/inja.sock" inject $KEY_F7 3 || true)
expect_contains inj_nobody_caps_denied "denied=0x8" "$out"
expect_contains inj_nobody_begin_denied "status=4" "$out"

# ---- 4: consumer level gate (Windows golden) ---------------------------------
cat > "$WORK/gate.ahk" <<'EOF'
#Requires AutoHotkey v2.0
#InputLevel 5
F7:: {
    FileAppend("fired`n", A_Args[1])
    ExitApp(0)
}
SetTimer(() => ExitApp(7), -6000)
EOF
run_gate() { # level expected_rc
  local level=$1 expect_rc=$2
  local before_sub
  before_sub=$(grep -c 'v2 subscribed' "$WORK/inj.log" 2>/dev/null || true)
  rm -f "$WORK/gate-result.txt" "$WORK/gate.log"
  ( cd "$WORK" && AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$WORK/inja.sock" \
      xvfb-run -a "$AHK" gate.ahk "$WORK/gate-result.txt" >"$WORK/gate.log" 2>&1 ) &
  local apid=$!
  echo $apid >"$WORK/gate.pid"
  for _ in $(seq 1 200); do
    current_sub=$(grep -c 'v2 subscribed' "$WORK/inj.log" 2>/dev/null || true)
    grep -q "broker mode active" "$WORK/gate.log" 2>/dev/null \
      && [ "$current_sub" -gt "$before_sub" ] && break
    sleep .05
  done
  grep -q "broker mode active" "$WORK/gate.log"
  sleep .1
  sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 "$level" >/dev/null
  for _ in $(seq 1 200); do
    [ -s "$WORK/gate-result.txt" ] && break
    kill -0 "$apid" 2>/dev/null || break
    sleep .05
  done
  wait "$apid"; local rc=$?
  [ "$rc" = "$expect_rc" ]
}
if run_gate 10 0; then
  expect inj_gate_fires fired "$(cat "$WORK/gate-result.txt" 2>/dev/null)"
else
  expect inj_gate_fires fired "rc-failure"
fi
if run_gate 5 7; then
  expect inj_gate_equal_level_nofire nofire "$([ -s "$WORK/gate-result.txt" ] && cat "$WORK/gate-result.txt" || echo nofire)"
else
  expect inj_gate_equal_level_nofire nofire "rc-failure"
fi

# ---- 5: commit balances a held key ------------------------------------------
"$PROBE" "$WORK/inja.sock" watch "65:0" --until-events 2 --timeout-ms 8000 >"$WORK/bal.out" 2>&1 &
echo $! >"$WORK/bal.pid"
sleep .4
sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 3 --down-only --txn 82 >/dev/null
for _ in $(seq 1 100); do grep -q "WATCH_END events=2" "$WORK/bal.out" 2>/dev/null && break; sleep .05; done
sudo -n kill "$(cat "$WORK/bal.pid")" 2>/dev/null || true
wait "$(cat "$WORK/bal.pid")" 2>/dev/null || true
bal=$(cat "$WORK/bal.out")
expect_contains inj_commit_balance_down "txn=82" "$bal"
expect_contains inj_commit_balance_down_ph "code=65 phase=0 value=1" "$bal"
expect_contains inj_commit_balance_up "code=65 phase=1 value=0" "$bal"

# ---- 6: owner crash -> auto-abort + balance ----------------------------------
"$PROBE" "$WORK/inja.sock" watch "65:0" --timeout-ms 9000 >"$WORK/crash.out" 2>&1 &
echo $! >"$WORK/crash.pid"
sleep .4
set +e
sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 3 --down-only --no-commit \
  --crash-after-events --txn 83 >"$WORK/crash-inj.out" 2>&1 &
echo $! >"$WORK/crash-inj.pid"
wait "$(cat "$WORK/crash-inj.pid")"
set -e
for _ in $(seq 1 100); do grep -q "aborted" "$WORK/inj.log" 2>/dev/null && break; sleep .05; done
sleep .2
sudo -n kill "$(cat "$WORK/crash.pid")" 2>/dev/null || true
wait "$(cat "$WORK/crash.pid")" 2>/dev/null || true
wait "$(cat "$WORK/crash-inj.pid")" 2>/dev/null || true
crash=$(cat "$WORK/crash.out")
expect_contains inj_crash_balanced "txn=83" "$crash"
expect_contains inj_crash_balanced_ph "code=65 phase=1 value=0" "$crash"
expect_contains inj_crash_broker_log "aborted" "$(grep -o 'inject txn 83 client [0-9]* aborted' "$WORK/inj.log" | head -1)"

# ---- 7: TTL expiry -----------------------------------------------------------
"$PROBE" "$WORK/inja.sock" watch "65:0" --timeout-ms 9000 >"$WORK/ttl.out" 2>&1 &
echo $! >"$WORK/ttl.pid"
sleep .4
sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 3 --down-only --no-commit --stay 4000 --txn 84 >"$WORK/ttl-inj.out" 2>&1 &
echo $! >"$WORK/ttl-inj.pid"
sleep 4
sudo -n kill "$(cat "$WORK/ttl.pid")" 2>/dev/null || true
kill_probe "txn 84"
wait "$(cat "$WORK/ttl.pid")" 2>/dev/null || true
wait "$(cat "$WORK/ttl-inj.pid")" 2>/dev/null || true
expect_contains inj_ttl_log "timed out" "$(grep -o 'inject txn 84 client [0-9]* timed out' "$WORK/inj.log" | head -1)"
expect_contains inj_ttl_balanced "txn=84" "$(cat "$WORK/ttl.out")"
expect_contains inj_ttl_balanced_ph "code=65 phase=1 value=0" "$(cat "$WORK/ttl.out")"

# ---- 8: restart -> old transaction STALE ------------------------------------
sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 3 --begin-only --no-commit --stay 2500 --txn 85 >"$WORK/restart-inj.out" 2>&1 &
echo $! >"$WORK/restart-inj.pid"
sleep .5
sudo -n kill -9 "$(cat "$WORK/inj.pid")" 2>/dev/null || true
sleep .3
sudo -n rm -f "$WORK/inja.sock" "$WORK/inja.sock.lock"
start_daemon inj a AHK_INPUTD_TEST_DEVICE="name:definitely-not-a-device"
sleep .4
out=$(sudo -n "$PROBE" "$WORK/inja.sock" inject-event $KEY_F7 3 --txn 85)
expect_contains inj_restart_stale "status=3" "$out"
kill_probe "txn 85"

# ---- 9: preflight event_count bound ------------------------------------------
out=$(sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 3 --pairs 300 || true)
expect_contains inj_preflight_bound "status=6" "$out"

# ---- 10: total transaction quota ---------------------------------------------
for i in $(seq 1 17); do
  sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 3 --begin-only --no-commit --stay 1500 --txn "$((100 + i))" \
    >"$WORK/quota-$i.out" 2>&1 &
done
sleep 2
quota_ok=0
for i in $(seq 1 17); do
  grep -q "status=5" "$WORK/quota-$i.out" 2>/dev/null && quota_ok=1
done
expect inj_total_quota 1 "$quota_ok"
kill_probe "txn 1"  # free the quota probes' transaction slots for case 11.
sleep .3

# ---- 11: v1 client observes injected events ----------------------------------
"$V1CLIENT" "$WORK/inja.sock" "65:0" --timeout-ms 5000 >"$WORK/v1.out" 2>&1 &
echo $! >"$WORK/v1.pid"
sleep .4
sudo -n "$PROBE" "$WORK/inja.sock" inject $KEY_F7 2 >/dev/null
sleep 1
sudo -n kill "$(cat "$WORK/v1.pid")" 2>/dev/null || true
wait "$(cat "$WORK/v1.pid")" 2>/dev/null || true
expect_contains inj_v1_observes "EVENT code=65 value=1" "$(cat "$WORK/v1.out")"

# ---- summary ----------------------------------------------------------------
{
  echo "{"
  echo "  \"schema\": 1,"
  echo "  \"oracle\": \"inputd-injection\","
  echo "  \"pass\": $PASS,"
  echo "  \"fail\": $FAIL,"
  echo "  \"failures\": \"$FAILURES\""
  echo "}"
} > "$SUMMARY"
echo "inputd-injection: PASS=$PASS FAIL=$FAIL"
cat "$SUMMARY"
[ "$FAIL" = 0 ] || exit 1
exit 0
