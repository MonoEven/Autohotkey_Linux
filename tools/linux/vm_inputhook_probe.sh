#!/bin/bash
# vm_inputhook_probe.sh -- run assert_inputhook under Xvfb, check partial output.
set -u
R=/tmp/xtest_ih_probe.txt
exec > "$R" 2>&1
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
cd /home/mono/Autohotkey_Linux/tests/doccheck || exit 1
pkill -9 -f 'Xvfb :99' 2>/dev/null; rm -f /tmp/.X99-lock; sleep 0.5
Xvfb :99 -screen 0 1024x768x24 > /tmp/ih_xvfb.log 2>&1 &
XV=$!
for w in 1 2 3 4 5 6 7 8; do
  if DISPLAY=:99 timeout 2 xdpyinfo >/dev/null 2>&1; then break; fi
  sleep 1
done
rm -f /tmp/ahk_dc_inputhook_out.txt
DISPLAY=:99 "$AHK" assert_inputhook.ahk > /tmp/ih_run.log 2>&1 &
PID=$!
t=0
while [ $t -lt 45 ]; do
  if ! kill -0 "$PID" 2>/dev/null; then break; fi
  sleep 1; t=$((t+1))
done
if kill -0 "$PID" 2>/dev/null; then
  echo "HUNG after ${t}s"
  kill -9 "$PID" 2>/dev/null
else
  echo "exited rc=$? after ${t}s"
fi
echo "--- partial out ---"
cat /tmp/ahk_dc_inputhook_out.txt 2>/dev/null || echo "(no out file)"
echo "--- stderr tail ---"
tail -5 /tmp/ih_run.log 2>/dev/null
kill "$XV" 2>/dev/null
echo "ih_probe_done=1"
