#!/bin/bash
# Parameterized reliability soak. CI uses a short profile; a nightly VM sets
# AHK_SOAK_SECONDS=86400 and runs the identical workload/oracle for 24 hours.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
seconds="${AHK_SOAK_SECONDS:-60}"
case "$seconds" in *[!0-9]*|'') echo MIXED_SOAK_BAD_DURATION; exit 2 ;; esac
[ "$seconds" -ge 5 ] && [ "$seconds" -le 90000 ] \
  || { echo MIXED_SOAK_BAD_DURATION; exit 2; }
cat >/tmp/ahk_mixed_soak.ahk <<'EOF'
#Requires AutoHotkey v2.0
global soakRounds := 0, soakHotkeys := 0, soakHotstrings := 0, soakClipboard := 0
global soakClipboardRetries := 0
global soakDeadline := A_TickCount + Integer(EnvGet("AHK_SOAK_SECONDS")) * 1000
SoakHotstring(*) {
    global soakHotstrings
    soakHotstrings += 1
}
SoakFinish() {
    global soakRounds, soakHotkeys, soakHotstrings, soakClipboard, soakClipboardRetries
    SetTimer(SoakTick, 0)
    FileAppend("rounds=" soakRounds " hotkeys=" soakHotkeys " hotstrings=" soakHotstrings
        " clipboard=" soakClipboard " clipboard_retries=" soakClipboardRetries "`n", "/tmp/ahk_mixed_soak.out")
    ExitApp(soakRounds = soakHotkeys && soakRounds = soakHotstrings
        && soakRounds = soakClipboard ? 0 : 7)
}
SoakTick() {
    global soakRounds, soakClipboard, soakClipboardRetries, soakDeadline
    if A_TickCount >= soakDeadline {
        SoakFinish()
        return
    }
    soakRounds += 1
    expected := "mixed-soak-" soakRounds "-世界"
    A_Clipboard := expected
    Loop 20 {
        if A_Clipboard = expected {
            soakClipboard += 1
            break
        }
        soakClipboardRetries += 1
        Sleep(10)
    }
    SendLevel(0)
    Send("{F6}")
    Sleep(20) ; yield so the hotkey thread can interrupt this timer callback
    SendLevel(1)
    SendText("zzq")
    Sleep(20)
    SendLevel(0)
}
F6::{
    global soakHotkeys
    soakHotkeys += 1
}
Hotstring(":B0*:zzq", SoakHotstring)
SetTimer(SoakTick, 200)
SetTimer(SoakFinish, -Integer(EnvGet("AHK_SOAK_SECONDS")) * 1000 - 1500)
EOF
rm -f /tmp/ahk_mixed_soak.out /tmp/ahk_mixed_soak.rss /tmp/ahk_mixed_soak.log
export AHK_SOAK_SECONDS="$seconds"
"$BIN" /tmp/ahk_mixed_soak.ahk >/tmp/ahk_mixed_soak.log 2>&1 &
pid=$!
start_epoch=$(date +%s)
while kill -0 "$pid" 2>/dev/null; do
  rss=$(awk '/^VmRSS:/{print $2}' "/proc/$pid/status" 2>/dev/null)
  [ -z "$rss" ] || printf '%s %s\n' "$(date +%s)" "$rss" >>/tmp/ahk_mixed_soak.rss
  sleep 1
done
wait "$pid"; rc=$?
end_epoch=$(date +%s)
[ "$rc" = 0 ] && [ -s /tmp/ahk_mixed_soak.out ] \
  || { echo "MIXED_SOAK_RUNTIME_FAIL rc=$rc"; cat /tmp/ahk_mixed_soak.out /tmp/ahk_mixed_soak.log; exit 1; }
rounds=$(sed -n 's/.*rounds=\([0-9][0-9]*\).*/\1/p' /tmp/ahk_mixed_soak.out)
hotkeys=$(sed -n 's/.*hotkeys=\([0-9][0-9]*\).*/\1/p' /tmp/ahk_mixed_soak.out)
hotstrings=$(sed -n 's/.*hotstrings=\([0-9][0-9]*\).*/\1/p' /tmp/ahk_mixed_soak.out)
clipboard=$(sed -n 's/.*clipboard=\([0-9][0-9]*\).*/\1/p' /tmp/ahk_mixed_soak.out)
clipboard_retries=$(sed -n 's/.*clipboard_retries=\([0-9][0-9]*\).*/\1/p' /tmp/ahk_mixed_soak.out)
# ASan is deliberately slower; gate correctness and at least two complete
# mixed rounds/second rather than imposing native-build throughput.
minimum=$((seconds * 2))
[ -n "$rounds" ] && [ "$rounds" -ge "$minimum" ] \
  && [ "$hotkeys" = "$rounds" ] && [ "$hotstrings" = "$rounds" ] \
  && [ "$clipboard" = "$rounds" ] \
  || { echo MIXED_SOAK_COUNT_FAIL; cat /tmp/ahk_mixed_soak.out; exit 1; }
# Exclude the first five startup samples (dynamic libraries/GTK/XKB warm-up)
# from the leak slope; fall back to the first sample for very short runs.
rss_start=$(awk 'NR==1{first=$2} NR==6{warm=$2} END{print warm ? warm : first}' /tmp/ahk_mixed_soak.rss)
rss_end=$(awk 'END{print $2}' /tmp/ahk_mixed_soak.rss)
rss_max=$(awk 'BEGIN{m=0} $2>m{m=$2} END{print m}' /tmp/ahk_mixed_soak.rss)
rss_min=$(awk 'NR==1{m=$2} $2<m{m=$2} END{print m+0}' /tmp/ahk_mixed_soak.rss)
: "${rss_start:=0}" "${rss_end:=0}" "${rss_max:=0}" "${rss_min:=0}"
rss_growth=$((rss_end - rss_start))
elapsed=$((end_epoch - start_epoch))
[ "$elapsed" -gt 0 ] || elapsed=1
slope=$(awk -v g="$rss_growth" -v e="$elapsed" 'BEGIN{printf "%.2f", g*60/e}')
profile=native
rss_gate=true
case "$BIN" in
  *asan*) profile=asan; rss_gate=false ;;
esac
result=pass
# ASan intentionally retains freed blocks in its quarantine, producing a linear
# RSS ramp under this allocation-heavy workload. Event consistency still gates
# ASan; the native build is the authoritative 20 MiB warm-RSS slope gate.
if [ "$rss_gate" = true ] && [ "$rss_growth" -ge 20480 ]; then
  result=fail
fi
cat >"$OUT/mixed-soak-summary.json" <<EOF
{"schema":1,"result":"$result","profile":"$profile","profile_seconds":$seconds,"elapsed_seconds":$elapsed,"rounds":$rounds,"hotkeys":$hotkeys,"hotstrings":$hotstrings,"clipboard":$clipboard,"clipboard_retries":$clipboard_retries,"clipboard_wait_budget_ms":200,"rss_gate":$rss_gate,"rss_start_kb":$rss_start,"rss_end_kb":$rss_end,"rss_min_kb":$rss_min,"rss_max_kb":$rss_max,"rss_growth_kb":$rss_growth,"rss_slope_kb_per_min":$slope}
EOF
if [ "$result" != pass ]; then
  echo "MIXED_SOAK_RSS_FAIL growth_kb=$rss_growth"
  cat /tmp/ahk_mixed_soak.rss
  exit 1
fi
echo "MIXED_SOAK_PASS profile=$profile seconds=$seconds rounds=$rounds clipboard_retries=$clipboard_retries rss_gate=$rss_gate rss_growth_kb=$rss_growth slope_kb_per_min=$slope"
