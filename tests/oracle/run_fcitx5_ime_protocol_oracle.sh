#!/bin/bash
# CI-capable protocol oracle. This validates the documented Fcitx5 D-Bus path,
# not a real Fcitx desktop installation (the latter remains explicitly open).
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
command -v dbus-run-session >/dev/null || { echo FCITX5_IME_SKIP dbus-run-session-missing; exit 2; }
${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/fcitx5-signal-probe" \
  "$ROOT/tests/oracle/fcitx5_signal_probe.c" $(pkg-config --cflags --libs dbus-1) || exit 2
cat >/tmp/fcitx5_ime.ahk <<'EOF'
#Requires AutoHotkey v2.0
Hit(*) {
    s := ImeStatus()
    FileAppend("hit=1 framework=" s.Framework " engine=" s.Engine " listening=" s.Listening " preedit=" s.Preedit " commits=" s.Commits " last=" s.LastCommit "`n", "/tmp/fcitx5_ime_hit")
    ExitApp
}
Hotstring(":B0*:你好", Hit)
s := ImeStatus()
FileAppend("ready=" s.Framework ":" s.Engine ":" s.Listening ":" s.Scope "`n", "/tmp/fcitx5_ime_ready")
SetTimer(() => ExitApp(3), -10000)
EOF
rm -f /tmp/fcitx5_ime_ready /tmp/fcitx5_ime_hit /tmp/fcitx5_ime_dump \
  /tmp/fcitx5_ime_trace /tmp/fcitx5_probe_ready /tmp/fcitx5_session.log
export ROOT BIN OUT
dbus-run-session -- bash -c '
  set -u
  "$OUT/fcitx5-signal-probe" /tmp/fcitx5_probe_ready >/tmp/fcitx5_probe.log 2>&1 &
  producer=$!
  for _ in $(seq 1 100); do test -f /tmp/fcitx5_probe_ready && break; sleep .02; done
  AHK_IME_DUMP=/tmp/fcitx5_ime_dump AHK_INPUT_EVENT_TRACE=/tmp/fcitx5_ime_trace \
    "$BIN" /tmp/fcitx5_ime.ahk >/tmp/fcitx5_ime_ahk.log 2>&1 &
  ahk=$!
  for _ in $(seq 1 200); do test -f /tmp/fcitx5_ime_ready && break; sleep .02; done
  grep -q "^ready=fcitx5:pinyin:1:eavesdrop$" /tmp/fcitx5_ime_ready || exit 10
  kill -USR1 "$producer"
  wait "$ahk" || exit 11
  grep -q "^hit=1 framework=fcitx5 engine=pinyin listening=1 preedit=0 commits=1 last=你好$" /tmp/fcitx5_ime_hit || exit 12
  kill "$producer" 2>/dev/null; wait "$producer" 2>/dev/null || true
' >/tmp/fcitx5_session.log 2>&1
rc=$?
[ "$rc" = 0 ] || { echo "FCITX5_IME_PROTOCOL_FAIL rc=$rc"; cat /tmp/fcitx5_session.log /tmp/fcitx5_ime_ahk.log /tmp/fcitx5_ime_dump; exit 1; }
commit_count=$(grep -c '"source":"ime_commit"' /tmp/fcitx5_ime_trace 2>/dev/null || true)
[ "$commit_count" = 2 ] \
  && grep -q '"text":20320.*"origin":"fcitx5"' /tmp/fcitx5_ime_trace \
  && grep -q '"text":22909.*"origin":"fcitx5"' /tmp/fcitx5_ime_trace \
  || { echo FCITX5_IME_EVENT_FAIL; cat /tmp/fcitx5_ime_trace; exit 1; }
cat >"$OUT/fcitx5-ime-protocol-summary.json" <<EOF
{"schema":1,"result":"pass","scope":"protocol","framework":"fcitx5","engine":"pinyin","preedit":"nihao","commit":"你好","hotstring":true,"ime_event_count":2,"event_origin":"fcitx5","real_desktop_e2e":false}
EOF
echo "FCITX5_IME_PROTOCOL_ORACLE_PASS engine=pinyin commit=你好 hotstring=1 events=2 real_desktop_e2e=0"
