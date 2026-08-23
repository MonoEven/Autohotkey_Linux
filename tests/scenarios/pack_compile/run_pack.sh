#!/bin/bash
# pack_compile scenario: --pack + run the packed binary + verify A_IsCompiled.
set -u
AHK="${AHK:?runner must export AHK}"
rm -f /tmp/scn_pack_compile /tmp/scn_pack_out.txt
cat > /tmp/scn_pack_src.ahk <<'EOF'
#Requires AutoHotkey v2.0
FileAppend("compiled:" A_IsCompiled "`n", "/tmp/scn_pack_out.txt")
ExitApp
EOF
"$AHK" --pack /tmp/scn_packed /tmp/scn_pack_src.ahk >/dev/null 2>&1
rm -f /tmp/scn_pack_out.txt
/tmp/scn_packed
if [ -f /tmp/scn_pack_out.txt ] && grep -q 'compiled:1' /tmp/scn_pack_out.txt; then
  touch /tmp/scn_pack_compile
fi
exit 0