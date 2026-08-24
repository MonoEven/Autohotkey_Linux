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
echo "DBGP_ORACLE_PASS breakpoint=3 step=4 array_pages=2 map=101,202 nested=42 exception=6:D3-boom detach=1"
