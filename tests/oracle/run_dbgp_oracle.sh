#!/bin/bash
# Independent IDE-side acceptance for the Linux DBGp engine.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
test -x "$BIN" || { echo "DBGP_ORACLE_FAIL missing-runtime=$BIN"; exit 1; }
python3 "$ROOT/tests/oracle/dbgp_server_oracle.py" \
  "$BIN" "$ROOT/tests/oracle/dbgp_fixture.ahk" \
  --summary "$OUT/dbgp-summary.json" || exit 1
echo "DBGP_ORACLE_PASS breakpoint=3 step=4 array_pages=2 map=101,202 nested=42 dbus_proxy=1 typed_scalar=42 exception=6:D3-boom idle_pause_lt500ms=1 idle_value=77 detach=1 reconnect=2 running_reconnect=1 ide_crash_cleanup=1 stop=1"
