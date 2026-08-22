#!/bin/bash
# vm_strict_test.sh -- verify AHK_STRICT_PARITY warn/error modes.
set -u
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
R=/tmp/xtest_strict.txt
exec > "$R" 2>&1
cat > /tmp/sp.ahk <<'EOF'
#Requires AutoHotkey v2.0
RegRead("HKCU", "Software\\AHK_DC_Test")
MsgBox "reg_ok=1"
ExitApp
EOF
echo "=== no env: no strict warning ==="
env -u AHK_STRICT_PARITY "$AHK" /tmp/sp.ahk > /tmp/sp_none.out 2>&1
grep -c "strict-parity" /tmp/sp_none.out || true
grep "reg_ok" /tmp/sp_none.out

echo "=== warn mode: warning printed, function still works ==="
AHK_STRICT_PARITY=warn "$AHK" /tmp/sp.ahk > /tmp/sp_warn.out 2>&1
grep "strict-parity" /tmp/sp_warn.out
grep "reg_ok" /tmp/sp_warn.out

echo "=== error mode: raises, function not called ==="
AHK_STRICT_PARITY=error "$AHK" /tmp/sp.ahk > /tmp/sp_err.out 2>&1
grep "AHK_STRICT_PARITY=error" /tmp/sp_err.out
grep "reg_ok" /tmp/sp_err.out || echo "reg_not_reached"

echo "=== warn mode, P1 function: no warning ==="
cat > /tmp/sp2.ahk <<'EOF'
#Requires AutoHotkey v2.0
MsgBox "m1=" A_ParityLevel("MsgBox")
ExitApp
EOF
AHK_STRICT_PARITY=warn "$AHK" /tmp/sp2.ahk > /tmp/sp2_warn.out 2>&1
grep -c "strict-parity" /tmp/sp2_warn.out || true
grep "m1" /tmp/sp2_warn.out

echo "=== error mode, P4 ComObjArray (already errors) ==="
cat > /tmp/sp3.ahk <<'EOF'
#Requires AutoHotkey v2.0
try {
    ComObjArray(0x0003, 2)
    FileAppend("ca:noerr`n", "/tmp/sp3_out.txt")
} catch as e {
    FileAppend("ca:err:" e.Message "`n", "/tmp/sp3_out.txt")
}
ExitApp
EOF
rm -f /tmp/sp3_out.txt
AHK_STRICT_PARITY=error "$AHK" /tmp/sp3.ahk > /tmp/sp3_err.out 2>&1
head -2 /tmp/sp3_err.out
cat /tmp/sp3_out.txt 2>/dev/null
echo "strict_done=1"
