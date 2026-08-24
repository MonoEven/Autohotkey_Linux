#!/bin/bash
# M5-A oracle: EWMH _NET_CLIENT_LIST must win over raw XQueryTree; without the
# property the fallback (raw children) keeps both windows visible.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
CC="${CC:-cc}"
$CC -O2 -Wall -Wextra -o "$OUT/x11-ewmh" "$ROOT/tests/oracle/x11_ewmh.c" \
  $(pkg-config --cflags --libs x11) || exit 2

cat >/tmp/m5a.ahk <<'EOF'
#Requires AutoHotkey v2.0
a := WinGetList("EWMH-A").Length
b := WinGetList("EWMH-B").Length
FileAppend("a=" a " b=" b "`n", "/tmp/m5a.out")
ExitApp
EOF

run_case() {
  local extra="$1" ids
  rm -f /tmp/m5a.out /tmp/m5a_ids.txt
  xvfb-run -a bash -c "export BIN='$BIN' OUT='$OUT' EXTRA='$extra'; \
    \"\$OUT/x11-ewmh\" \$EXTRA >/tmp/m5a_ids.txt 2>&1 & EPID=\$!; sleep .5; \
    \"\$BIN\" /tmp/m5a.ahk >/tmp/m5a_ahk.log 2>&1; kill \$EPID 2>/dev/null; wait \$EPID 2>/dev/null"
}

run_case ""
EWMH_RESULT="$(cat /tmp/m5a.out 2>/dev/null)"
[ "$EWMH_RESULT" = "a=1 b=0" ] || { echo "EWMH_WIN_FAIL result=[$EWMH_RESULT]"; cat /tmp/m5a_ahk.log 2>/dev/null; exit 1; }

run_case "--no-client-list"
FALLBACK_RESULT="$(cat /tmp/m5a.out 2>/dev/null)"
[ "$FALLBACK_RESULT" = "a=1 b=1" ] || { echo "FALLBACK_WIN_FAIL result=[$FALLBACK_RESULT]"; cat /tmp/m5a_ahk.log 2>/dev/null; exit 1; }

cat >"$OUT/ewmh-summary.json" <<EOF
{"schema":1,"result":"pass","ewmh":{"A":1,"B":0},"fallback":{"A":1,"B":1}}
EOF
echo "EWMH_ORACLE_PASS ewmh=[a=1 b=0] fallback=[a=1 b=1]"
