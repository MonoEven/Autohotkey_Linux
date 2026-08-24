#!/bin/bash
# D2: external DAP client drives the extension's adapter over the real DBGp core.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
command -v node >/dev/null || { echo DAP_ADAPTER_ORACLE_SKIP node-missing; exit 2; }
node "$ROOT/tests/oracle/dap_adapter_oracle.js" \
  "$BIN" "$ROOT/tests/oracle/dbgp_fixture.ahk" \
  "$OUT/dap-adapter-summary.json" || exit 1
echo "DAP_ADAPTER_ORACLE_PASS breakpoint=3 step=4 x=10 array_pages=2 array_count=20 alpha=A beta=42 map=101,202 y=15 exception=6:D3-boom terminated=1"
