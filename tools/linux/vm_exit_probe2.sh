#!/bin/bash
# vm_exit_probe2.sh -- does the §4 binary hang at exit? (no dialogs).
set -u
R=/tmp/xtest_exit_probe2.txt
exec > "$R" 2>&1
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
pkill -9 -f 'Xvfb :99' 2>/dev/null; rm -f /tmp/.X99-lock; sleep 0.5
Xvfb :99 -screen 0 1024x768x24 > /tmp/xp2_xvfb.log 2>&1 &
XV=$!
for w in 1 2 3 4 5 6 7 8; do
  if DISPLAY=:99 timeout 2 xdpyinfo >/dev/null 2>&1; then break; fi
  sleep 1
done
# 1) trivial persistent + timer-exit
cat > /tmp/ex2.ahk <<'EOF'
#Requires AutoHotkey v2.0
SetTimer(() => ExitApp(0), 2000)
EOF
start=$(date +%s)
DISPLAY=:99 "$AHK" /tmp/ex2.ahk > /tmp/ex2.out 2>&1
rc=$?
end=$(date +%s)
echo "timer-exit rc=$rc elapsed=$((end-start))s"
# 2) persistent + ExitApp immediately
cat > /tmp/ex3.ahk <<'EOF'
#Requires AutoHotkey v2.0
ExitApp 0
EOF
start=$(date +%s)
DISPLAY=:99 "$AHK" /tmp/ex3.ahk > /tmp/ex3.out 2>&1
rc=$?
end=$(date +%s)
echo "immediate-exit rc=$rc elapsed=$((end-start))s"
# 3) a hotkey-registering script (registers a hotkey -> captures) then exits
cat > /tmp/ex4.ahk <<'EOF'
#Requires AutoHotkey v2.0
Hotkey("F12", (*) => 0)
Sleep 200
ExitApp 0
EOF
start=$(date +%s)
DISPLAY=:99 "$AHK" /tmp/ex4.ahk > /tmp/ex4.out 2>&1
rc=$?
end=$(date +%s)
echo "hotkey-exit rc=$rc elapsed=$((end-start))s"
kill "$XV" 2>/dev/null
echo "exit_probe2_done=1"
