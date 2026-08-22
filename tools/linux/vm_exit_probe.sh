#!/bin/bash
# vm_exit_probe.sh -- does the §4 binary hang at exit for a trivial script?
set -u
R=/tmp/xtest_exit_probe.txt
exec > "$R" 2>&1
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
cd /home/mono/Autohotkey_Linux/tests/doccheck || exit 1
pkill -9 -f 'Xvfb :99' 2>/dev/null; rm -f /tmp/.X99-lock; sleep 0.5
Xvfb :99 -screen 0 1024x768x24 > /tmp/xp_xvfb.log 2>&1 &
XV=$!
for w in 1 2 3 4 5 6 7 8; do
  if DISPLAY=:99 timeout 2 xdpyinfo >/dev/null 2>&1; then break; fi
  sleep 1
done
cat > /tmp/ex.ahk <<'EOF'
#Requires AutoHotkey v2.0
MsgBox "hello"
ExitApp 0
EOF
echo "--- trivial MsgBox+Exit ---"
start=$(date +%s)
DISPLAY=:99 "$AHK" /tmp/ex.ahk > /tmp/ex.out 2>&1
rc=$?
end=$(date +%s)
echo "rc=$rc elapsed=$((end-start))s"
echo "--- trivial persistent + ExitApp after 2s ---"
cat > /tmp/ex2.ahk <<'EOF'
#Requires AutoHotkey v2.0
SetTimer(() => ExitApp(0), 2000)
EOF
start=$(date +%s)
DISPLAY=:99 "$AHK" /tmp/ex2.ahk > /tmp/ex2.out 2>&1
rc=$?
end=$(date +%s)
echo "rc=$rc elapsed=$((end-start))s"
kill "$XV" 2>/dev/null
echo "exit_probe_done=1"
