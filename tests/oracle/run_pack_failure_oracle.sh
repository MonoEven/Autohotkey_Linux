#!/bin/bash
# --pack negative-path oracle (AutoHotkey_Linux_Audit_47d1fcc I08).
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
AHK="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
WORK=/tmp/ahk-pack-fault
rm -rf "$WORK"; mkdir -p "$WORK"
printf 'ORIGINAL_OUTPUT\n' > "$WORK/existing"
cat > "$WORK/missing.ahk" <<'EOF'
#Requires AutoHotkey v2.0
FileInstall("/definitely/missing/ahk-pack-resource.bin", A_Temp "/unused.bin")
FileAppend("should-not-run", A_Temp "/pack-fault-run")
EOF
"$AHK" --pack "$WORK/existing" "$WORK/missing.ahk" >"$WORK/missing.log" 2>&1
rc=$?
if [ "$rc" = 0 ]; then
  echo PACK_FAULT_FAIL missing-resource-accepted
  cat "$WORK/missing.log"
  exit 1
fi
if ! grep -q '^ORIGINAL_OUTPUT$' "$WORK/existing"; then
  echo PACK_FAULT_FAIL existing-output-modified
  exit 1
fi
if [ -e "$WORK/existing.tmp."* ]; then
  echo PACK_FAULT_FAIL temporary-output-left-behind
  exit 1
fi
echo PACK_FAULT_PASS missing_resource=1 existing_output_preserved=1 temp_clean=1
