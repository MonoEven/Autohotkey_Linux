#!/bin/bash
# vm_ai2.sh -- run assert_input under Xvfb (healthy), bounded.
set -u
pkill -9 -f 'Xvfb :99' 2>/dev/null; rm -f /tmp/.X99-lock; sleep 0.5
Xvfb :99 -screen 0 1024x768x24 > /tmp/ai_xvfb.log 2>&1 &
XV=$!
for i in 1 2 3 4 5 6 7 8; do
  if DISPLAY=:99 timeout 2 xdpyinfo >/dev/null 2>&1; then break; fi
  sleep 1
done
R=/tmp/xtest_ai_result.txt
exec > "$R" 2>&1
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
cd /home/mono/Autohotkey_Linux/tests/doccheck || exit 1
rm -f /tmp/ahk_dc_input_out.txt
DISPLAY=:99 "$AHK" assert_input.ahk > /tmp/ai_run.log 2>&1 &
PID=$!
i=0
while [ $i -lt 90 ]; do
  if ! kill -0 "$PID" 2>/dev/null; then break; fi
  sleep 1; i=$((i+1))
done
if kill -0 "$PID" 2>/dev/null; then
  echo "ai=HANG (killed at ${i}s)"; kill -9 "$PID" 2>/dev/null
else
  wait "$PID"; echo "ai=EXITED rc=$? elapsed=${i}s"
fi
kill "$XV" 2>/dev/null
echo "--- produced vs expect ---"
diff <(sort /tmp/ahk_dc_input_out.txt 2>/dev/null) <(sort assert_input_expect.txt) && echo "DIFF_CLEAN"
echo "--- new assertions ---"
grep -E '^(sendlevel|sendevent_delay|sendinput_100|sendinput_self|sendevent_self)' /tmp/ahk_dc_input_out.txt 2>/dev/null
echo "ai_done=1"
