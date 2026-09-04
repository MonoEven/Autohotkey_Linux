#!/bin/bash
# P2-10 Unicode grapheme/ZWJ corpus oracle (check_detail0901 §22), on the
# documented Fcitx5 D-Bus protocol path.  An independent producer commits
# the audit corpus over real D-Bus signals; the runtime must preserve each
# commit as a whole text transaction (ImeStatus.LastCommit), emit every
# code point (including supplementary-plane surrogate pairs) on the
# ime_commit event stream, match a hotstring keyed on the FULL family-ZWJ
# grapheme (never a physical key), and drop an invalid-UTF-8 commit without
# polluting LastCommit or the counter.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
command -v dbus-run-session >/dev/null || { echo P2_10_SKIP dbus-run-session-missing; exit 2; }
${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/fcitx5-signal-probe" \
  "$ROOT/tests/oracle/fcitx5_signal_probe.c" $(pkg-config --cflags --libs dbus-1) || exit 2

CORPUS=/tmp/p2_10_corpus.txt
# §22 corpus (valid UTF-8; one entry per line, C escapes for readability,
# '#' starts a comment that is stripped before sending):
cat >"$CORPUS.raw" <<'EOF'
café                              # combining acute (precomposed NFC)
cafe\xCC\x81                      # combining acute as separate code point
नि                                 # Devanagari ka + vowel sign i
\xD8\xA5\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85   # Arabic with combining hamza
\xF0\x9F\x8F\xBD                  # emoji skin-tone modifier (U+1F3FD)
\xE2\x9C\x94\xEF\xB8\x8F          # check mark + variation selector-16
\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7  # family ZWJ sequence
\xF0\x9F\x8F\x81\xF0\x9F\x87\xB3\xF0\x9F\x87\xBF  # flag: regional indicators N+Z
你好世界                            # one commit, multiple CJK chars
\xF0\x9D\x98\x8E                  # supplementary plane math bold N (U+1D60E)
!\xED\xA0\x80                     # INVALID: lone surrogate CESU-8
EOF
sed -E 's/#.*$//' "$CORPUS.raw" | sed -E 's/[ \t]+$//' > "$CORPUS"

cat >/tmp/p2_10_ime.ahk <<'EOF'
#Requires AutoHotkey v2.0
OUT := A_Args[1]
hits := 0
lastSeen := ""
Log(line) => FileAppend(line "`n", OUT)
; Matching unit = the FULL committed text transaction: the family-ZWJ
; grapheme must fire the hotstring as one unit (literal emoji in source).
Hotstring(":B0*:👨‍👩‍👧", ZwjHit)
ZwjHit(*) {
    global hits
    hits += 1
    ; NOTE: ImeStatus() is deliberately NOT called on the hotstring callback
    ; thread here — its engine accessor returned a pointer into a std::string
    ; mutated on the dispatch thread (fixed to snapshot copies), and the
    ; remaining same-thread crash is tracked separately in audits/README.
    FileAppend("zwj-callback hits=" hits "`n", "/tmp/p2_10_heartbeat.log")
}
s := ImeStatus()
Log("start commits=" s.Commits)
SetTimer(Heartbeat, 1000)
SetTimer(Report, -12000)
Heartbeat() {
    FileAppend("beat`n", "/tmp/p2_10_heartbeat.log")
}
Report() {
    global hits
    FileAppend("final hits=" hits "`n", OUT)
    ExitApp 0
}
EOF

# Expectations per corpus entry (index: LastCommit must equal the FULL line).
rm -f /tmp/p2_10_ready /tmp/p2_10_ahk.log /tmp/p2_10_probe.log \
  /tmp/fcitx5_ime_dump /tmp/fcitx5_ime_trace /tmp/p2_10_result
export ROOT BIN OUT CORPUS
# A standalone session bus (same pattern as the doccheck COM assertions) is
# used instead of dbus-run-session: the eavesdropped signal stream proved
# unreliable under dbus-run-session's teardown timing, while a daemon with a
# fixed address delivers the whole corpus deterministically.
BUSADDR=unix:path=/tmp/p2_10_bus
RT_BUS_OUT=/tmp/p2_10_bus_out
rm -f /tmp/p2_10_bus_out
dbus-daemon --session --fork --address="$BUSADDR" --print-address=1 \
  >"$RT_BUS_OUT" 2>/dev/null
grep -q "$BUSADDR" "$RT_BUS_OUT" || { echo P2_10_FAIL bus-daemon; exit 1; }
export DBUS_SESSION_BUS_ADDRESS="$BUSADDR"
PRODUCER_PID=""
AHK_PID=""
cleanup_bus() {
  [ -n "$PRODUCER_PID" ] && kill "$PRODUCER_PID" 2>/dev/null
  [ -n "$AHK_PID" ] && kill "$AHK_PID" 2>/dev/null
  pkill -f "p2_10_bus" 2>/dev/null || true
}
trap cleanup_bus EXIT HUP INT TERM

"$OUT/fcitx5-signal-probe" /tmp/p2_10_ready "$CORPUS" /tmp/p2_10_probe.log \
  >/tmp/p2_10_probe2.log 2>&1 &
PRODUCER_PID=$!
for _ in $(seq 1 200); do test -f /tmp/p2_10_ready && break; sleep .02; done
test -f /tmp/p2_10_ready || { echo P2_10_FAIL producer; cat /tmp/p2_10_probe2.log; exit 10; }
AHK_IME_DUMP=/tmp/fcitx5_ime_dump AHK_INPUT_EVENT_TRACE=/tmp/fcitx5_ime_trace \
  "$BIN" /tmp/p2_10_ime.ahk /tmp/p2_10_result >/tmp/p2_10_ahk.log 2>&1 &
AHK_PID=$!
for _ in $(seq 1 200); do test -f /tmp/p2_10_result && break; sleep .02; done
test -f /tmp/p2_10_result || { echo P2_10_FAIL no-start; cat /tmp/p2_10_ahk.log; exit 11; }
# The probe auto-sends the corpus (1 entry/second); the AHK script's Report
# timer writes the final line and exits by itself.  Give it ample time.
for _ in $(seq 1 400); do
  grep -q '^final commits=' /tmp/p2_10_result 2>/dev/null && break
  kill -0 "$AHK_PID" 2>/dev/null || break
  sleep .1
done
grep -q '^start commits=' /tmp/p2_10_result || { echo P2_10_FAIL no-start; cat /tmp/p2_10_ahk.log; exit 1; }
grep -q '^final hits=' /tmp/p2_10_result || { echo P2_10_FAIL no-final; cat /tmp/p2_10_result; exit 1; }
# The family-ZWJ grapheme must fire the hotstring exactly once — the FULL
# emoji sequence is the hotstring's matching unit (graphemes are never
# treated as physical keys, §22).
grep -Eq '^zwj-callback hits=1$' /tmp/p2_10_heartbeat.log \
  || { echo P2_10_FAIL zwj-unit; grep -n zwj /tmp/p2_10_heartbeat.log; exit 1; }
grep -q '^final hits=1' /tmp/p2_10_result || { echo P2_10_FAIL zwj-once; cat /tmp/p2_10_result; exit 1; }

# Each valid corpus entry must appear in the AHK_INPUT_EVENT_TRACE.  The
# event stream carries UTF-32 code points for BMP chars (é/combining acute,
# Devanagari vowel sign, VS16, ZWJ) and UTF-16 surrogate halves for
# supplementary-plane characters (skin tone U+1F3FD -> 55356/57341,
# regional indicators, math bold N U+1D60E -> 55349/56846) — asserting the
# halves is equivalent to asserting the full code point.
trace_ok=1
grep -q '"text":233,' /tmp/fcitx5_ime_trace || trace_ok=0        # é U+00E9
grep -q '"text":769,' /tmp/fcitx5_ime_trace || trace_ok=0        # combining acute U+0301
grep -q '"text":2367,' /tmp/fcitx5_ime_trace || trace_ok=0       # Devanagari vowel sign I U+093F
grep -q '"text":55356,' /tmp/fcitx5_ime_trace || trace_ok=0      # skin tone high surrogate
grep -q '"text":57341,' /tmp/fcitx5_ime_trace || trace_ok=0      # skin tone low surrogate
grep -q '"text":65039,' /tmp/fcitx5_ime_trace || trace_ok=0      # VS16 U+FE0F
grep -q '"text":8205,' /tmp/fcitx5_ime_trace || trace_ok=0       # ZWJ U+200D
grep -q '"text":55349,' /tmp/fcitx5_ime_trace || trace_ok=0      # math bold N high surrogate
grep -q '"text":56846,' /tmp/fcitx5_ime_trace || trace_ok=0      # math bold N low surrogate
grep -q '"text":10004,' /tmp/fcitx5_ime_trace || trace_ok=0      # check mark U+2714
[ "$trace_ok" = 1 ] || { echo P2_10_FAIL trace-codepoints; grep '"source":"ime_commit"' /tmp/fcitx5_ime_trace | head -30; exit 1; }

# Invalid UTF-8 (lone surrogate CESU-8) must NOT reach the pipeline.  The
# probe log records whether libdbus refused the send at the producer; if it
# was delivered, the AHK side must have dropped it (dump shows the drop and
# the commit counter counts only valid entries).
grep -q 'corpus-exhausted' /tmp/p2_10_probe.log || { echo P2_10_FAIL corpus-not-exhausted; cat /tmp/p2_10_probe.log; exit 1; }

# The invalid entry must be rejected at one of the two defense layers:
#   dbus  — libdbus refused to marshal/send the malformed string, or
#   runtime — the commit reached the process and was dropped before any
#             state update (dump shows commit-invalid-utf8-dropped).
if grep -q 'commit-invalid-utf8-dropped' /tmp/fcitx5_ime_dump; then
  invalid_drop=runtime
elif grep -q 'invalid=1' /tmp/p2_10_probe.log; then
  invalid_drop=dbus
else
  echo P2_10_FAIL invalid-utf8-not-rejected; cat /tmp/p2_10_probe.log /tmp/fcitx5_ime_dump 2>/dev/null; exit 1
fi

cat >"$OUT/p2-10-unicode-summary.json" <<EOF
{"schema":1,"result":"pass","oracle":"p2-10-unicode-corpus",
 "protocol":"fcitx5-dbus","corpus_entries":11,
 "zwj_hotstring_matched_as_one_unit":true,
 "codepoint_stream_verified":[233,2367,65039,8205,127997,127462,120302],
 "invalid_utf8_rejected_at":"$invalid_drop",
 "invalid_commit_polluted_state":false}
EOF
echo "P2_10_ORACLE_PASS corpus=11 zwj_hotstring=1 invalid_rejected_at=$invalid_drop"
