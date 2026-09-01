#!/bin/bash
# M5b-2 InputHook/Hotstring normalized-consumer pipeline oracle.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER="${1:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
AHK="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BROKER" in /*) ;; *) BROKER="$ROOT/$BROKER" ;; esac
case "$AHK" in /*) ;; *) AHK="$ROOT/$AHK" ;; esac
OUT="$ROOT/tests/oracle/out"; mkdir -p "$OUT"
WORK=/tmp/input-consumer-pipeline; rm -rf "$WORK"; mkdir -p "$WORK"
FIXTURE="$WORK/inputd-test-fixture"
PROBE="$WORK/inputd-v2-probe"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$FIXTURE"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_v2_probe.c" -o "$PROBE"
sudo -n true 2>/dev/null || { echo "consumer pipeline oracle needs sudo -n"; exit 1; }
cleanup() {
  sudo -n pkill -9 -x ahk-inputd 2>/dev/null || true
  sudo -n pkill -9 -f "^$FIXTURE " 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
cleanup

cat >"$WORK/x11-consumers.ahk" <<'EOF'
#Requires AutoHotkey v2.0
#InputLevel 5
kd := 0, ku := 0, chars := "", selected := 0, eq := 0, gt := 0, gtlevel := -1
IhDown(hook, vk, sc) {
    global kd
    if (vk = 86)
        kd += 1
}
IhUp(hook, vk, sc) {
    global ku
    if (vk = 86)
        ku += 1
}
IhChar(hook, ch) {
    global chars
    chars .= ch
}
EqCb(*) {
    global eq
    eq += 1
}
GtCb(*) {
    global gt, gtlevel
    gt += 1
    gtlevel := A_SendLevel
}
Drive() {
    global ih, selected
    SendMode("Event")
    SendLevel(4)
    SendEvent("u")
    Sleep(450)
    SendLevel(5)
    SendEvent("v")
    Sleep(450)
    ih.Stop()
    Sleep(150)
    ihs := InputHook("VT3", "{Enter}")
    ihs.Start()
    ihs.KeyOpt("{Enter}", "S")
    SendLevel(6)
    SendEvent("m{Enter}")
    ihs.Wait(2000)
    selected := (ihs.Input = "m" && ihs.EndReason = "EndKey") ? 1 : 0
    Sleep(150)
    SendLevel(5)
    SendEvent("eqx ")
    Sleep(350)
    SendLevel(6)
    SendEvent("gtx")
}
Finish() {
    global kd, ku, chars, selected, eq, gt, gtlevel
    h := HotkeyBackendGet()
    FileAppend("kd=" kd " ku=" ku " chars=" chars " selected=" selected " eq=" eq " gt=" gt " gtlevel=" gtlevel " mode=" h.pipeline_mode "`n", A_Args[1])
    ExitApp(kd=1 && ku=1 && chars="v" && selected=1 && eq=0 && gt=1 && gtlevel=5 ? 0 : 7)
}
ih := InputHook("VI5")
ih.OnKeyDown := IhDown
ih.OnKeyUp := IhUp
ih.OnChar := IhChar
ih.Start()
Hotstring(":*:eqx", EqCb)
Hotstring(":*:gtx", GtCb)
SetTimer(Drive, -300)
SetTimer(Finish, -2600)
EOF
for mode in active mirror legacy; do
  rm -f "$WORK/x11-$mode.out" "$WORK/x11-$mode.trace"
  AHK_INPUT_PIPELINE="$mode" AHK_INPUT_PIPELINE_TRACE="$WORK/x11-$mode.trace" \
    xvfb-run -a "$AHK" "$WORK/x11-consumers.ahk" "$WORK/x11-$mode.out" \
    >"$WORK/x11-$mode.log" 2>&1
  grep -q "mode=$mode" "$WORK/x11-$mode.out"
done

start_fixture() { # name trigger code:value...
  local name=$1 trigger=$2; shift 2; local devfile="$WORK/$name.dev"
  sudo -n env AHK_FIXTURE_NAME="$name-$$" AHK_FIXTURE_DEVPATH="$devfile" \
    "$FIXTURE" --script-trigger "$trigger" "$@" >"$WORK/$name-fixture.log" 2>&1 &
  for _ in $(seq 1 100); do [ -s "$devfile" ] && break; sleep .03; done
  local node=$(cat "$devfile"); for _ in $(seq 1 100); do [ -e "$node" ] && break; sleep .03; done
  echo "$node"
}
start_broker() { # name node
  local name=$1 node=$2
  local sock="$WORK/$name.sock"
  sudo -n rm -f "$sock" "$sock.lock"
  sudo -n env AHK_INPUTD_TEST_DEVICE="$node" "$BROKER" --socket "$sock" --socket-mode 0666 -v >"$WORK/$name-broker.log" 2>&1 &
  for _ in $(seq 1 120); do grep -q grabbed "$WORK/$name-broker.log" 2>/dev/null && break; sleep .03; done
}
wait_subscribed() { # name expected-min-rules
  local name=$1 min=$2
  for _ in $(seq 1 200); do
    line=$(grep 'v2 subscribed' "$WORK/$name-broker.log" 2>/dev/null | tail -1 || true)
    count=$(echo "$line" | sed -nE 's/.*subscribed ([0-9]+) rule.*/\1/p')
    [ "${count:-0}" -ge "$min" ] && return 0
    sleep .03
  done
  return 1
}

cat >"$WORK/broker-ih.ahk" <<'EOF'
#Requires AutoHotkey v2.0
kd := 0, ku := 0, chars := ""
brokerIh_OnDown(h, vk, sc) {
    global kd
    if (vk = 65)
        kd += 1
}
brokerIh_OnUp(h, vk, sc) {
    global ku
    if (vk = 65)
        ku += 1
}
brokerIh_OnChar(h, ch) {
    global chars
    chars .= ch
}
Finish() {
    global kd,ku,chars
    h:=HotkeyBackendGet()
    FileAppend("kd=" kd " ku=" ku " chars=" chars " source=" h.state_source "`n",A_Args[1])
    ExitApp(kd=1 && ku=1 && chars="a" ? 0 : 8)
}
ih := InputHook("VT3")
ih.OnKeyDown := brokerIh_OnDown
ih.OnKeyUp := brokerIh_OnUp
ih.OnChar := brokerIh_OnChar
ih.Start()
FileAppend("ready`n",A_Args[2])
SetTimer(Finish,-3000)
EOF
cleanup; ihtrig="$WORK/broker-ih.trigger"; rm -f "$ihtrig"
ihnode=$(start_fixture broker-ih "$ihtrig" 30:1 30:0)
start_broker broker-ih "$ihnode"
( cd "$WORK" && AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$WORK/broker-ih.sock" \
  AHK_INPUT_PIPELINE=active AHK_INPUT_PIPELINE_TRACE="$WORK/broker-ih.trace" \
  xvfb-run -a "$AHK" broker-ih.ahk "$WORK/broker-ih.out" "$WORK/broker-ih.ready" >"$WORK/broker-ih.log" 2>&1 ) & AP=$!
for _ in $(seq 1 200); do [ -f "$WORK/broker-ih.ready" ] && break; sleep .03; done
wait_subscribed broker-ih 1
touch "$ihtrig"; wait "$AP"

cat >"$WORK/broker-hs.ahk" <<'EOF'
#Requires AutoHotkey v2.0
#InputLevel 0
OUT := A_Args[1]
hs_count := 0
hs_level := -1
brokerHs_Cb(ThisHotkey) {
    global hs_count, hs_level
    hs_count++
    hs_level := A_SendLevel
}
brokerHs_Finish() {
    global OUT, hs_count, hs_level
    h := HotkeyBackendGet()
    FileAppend("hs=" hs_count " level=" hs_level " source=" h.state_source "`n", OUT)
    ExitApp(hs_count = 1 && hs_level = 0 ? 0 : 9)
}
Hotstring(":*:gtx", brokerHs_Cb)
FileAppend("ready`n", A_Args[2])
SetTimer(brokerHs_Finish, -3500)
EOF
cleanup; hstrig="$WORK/broker-hs.trigger"; rm -f "$hstrig"
hsnode=$(start_fixture broker-hs "$hstrig" 34:1 34:0 20:1 20:0 45:1 45:0)
start_broker broker-hs "$hsnode"
( cd "$WORK" && AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$WORK/broker-hs.sock" \
  AHK_INPUT_PIPELINE=active AHK_INPUT_PIPELINE_TRACE="$WORK/broker-hs.trace" \
  xvfb-run -a "$AHK" broker-hs.ahk "$WORK/broker-hs.out" "$WORK/broker-hs.ready" >"$WORK/broker-hs.log" 2>&1 ) & AP=$!
for _ in $(seq 1 200); do [ -f "$WORK/broker-hs.ready" ] && break; sleep .03; done
wait_subscribed broker-hs 1
touch "$hstrig"; wait "$AP"

python3 "$ROOT/tests/oracle/verify_input_consumer_pipeline.py" "$WORK" \
  "$OUT/input-consumer-pipeline-summary.json"
echo "INPUT_CONSUMER_PIPELINE_ORACLE_PASS"
