#!/bin/bash
# vm_parity_test.sh -- test --parity and A_ParityLevel on the VM.
set -u
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
R=/tmp/xtest_parity.txt
exec > "$R" 2>&1
echo "=== --parity ==="
"$AHK" --parity ComObjArray
"$AHK" --parity SendInput
"$AHK" --parity MsgBox
"$AHK" --parity RegRead
"$AHK" --parity TrayTip
"$AHK" --parity NoSuchFn
echo "=== --parity missing arg ==="
"$AHK" --parity; echo "rc=$?"
echo "=== A_ParityLevel script ==="
cat > /tmp/pl.ahk <<'EOF'
#Requires AutoHotkey v2.0
FileAppend("pl_arr=" A_ParityLevel("ComObjArray") "`n", "/tmp/pl.txt")
FileAppend("pl_msg=" A_ParityLevel("MsgBox") "`n", "/tmp/pl.txt")
FileAppend("pl_sendplay=" A_ParityLevel("SendPlay") "`n", "/tmp/pl.txt")
FileAppend("pl_reg=" A_ParityLevel("RegRead") "`n", "/tmp/pl.txt")
FileAppend("pl_unknown=" A_ParityLevel("NoSuchFn") "`n", "/tmp/pl.txt")
ExitApp
EOF
rm -f /tmp/pl.txt
"$AHK" /tmp/pl.ahk
cat /tmp/pl.txt 2>/dev/null
echo "parity_done=1"
