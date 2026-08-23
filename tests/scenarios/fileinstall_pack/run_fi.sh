#!/bin/bash
# fileinstall_pack scenario: --pack embeds a FileInstall resource; the packed
# binary extracts it to the destination.
set -u
AHK="${AHK:?runner must export AHK}"
rm -f /tmp/scn_fileinstall_pack /tmp/scn_fi_result.txt
rm -rf /tmp/scn_fi; mkdir -p /tmp/scn_fi
printf 'PACKED-RESOURCE-OK' > /tmp/scn_fi/src.txt
cat > /tmp/scn_fi/main.ahk <<'EOF'
#Requires AutoHotkey v2.0
FileInstall("/tmp/scn_fi/src.txt", "/tmp/scn_fi/dest.txt")
c := FileExist("/tmp/scn_fi/dest.txt") ? FileRead("/tmp/scn_fi/dest.txt") : ""
FileAppend("ok=" (c = "PACKED-RESOURCE-OK" ? 1 : 0) "`n", "/tmp/scn_fi_result.txt")
ExitApp
EOF
"$AHK" --pack /tmp/scn_fi/packed /tmp/scn_fi/main.ahk >/dev/null 2>&1
/tmp/scn_fi/packed >/dev/null 2>&1
if [ -f /tmp/scn_fi_result.txt ] && grep -q 'ok=1' /tmp/scn_fi_result.txt; then
  touch /tmp/scn_fileinstall_pack
fi
exit 0