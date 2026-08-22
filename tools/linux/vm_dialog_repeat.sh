#!/bin/bash
# vm_dialog_repeat.sh -- run assert_dialog 3x under Xvfb to check flake stability.
set -u
R=/tmp/xtest_dialog_repeat.txt
exec > "$R" 2>&1
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
cd /home/mono/Autohotkey_Linux/tests/doccheck || exit 1
for i in 1 2 3; do
  pkill -9 -f 'Xvfb :99' 2>/dev/null; rm -f /tmp/.X99-lock; sleep 0.5
  Xvfb :99 -screen 0 1024x768x24 > /tmp/dg_xvfb.log 2>&1 &
  XV=$!
  for w in 1 2 3 4 5 6 7 8; do
    if DISPLAY=:99 timeout 2 xdpyinfo >/dev/null 2>&1; then break; fi
    sleep 1
  done
  rm -f /tmp/ahk_dc_dialog_out.txt
  DISPLAY=:99 "$AHK" assert_dialog.ahk > /tmp/dg_${i}.log 2>&1 &
  PID=$!
  t=0
  while [ $t -lt 60 ]; do
    if ! kill -0 "$PID" 2>/dev/null; then break; fi
    sleep 1; t=$((t+1))
  done
  if kill -0 "$PID" 2>/dev/null; then echo "run$i=HANG"; kill -9 "$PID" 2>/dev/null; else wait "$PID"; echo "run$i=EXIT rc=$? in ${t}s"; fi
  kill "$XV" 2>/dev/null
  if diff <(sort /tmp/ahk_dc_dialog_out.txt 2>/dev/null) <(sort assert_dialog_expect.txt) >/dev/null 2>&1; then
    echo "run$i=DIFF_CLEAN"
  else
    echo "run$i=MISMATCH:"
    diff <(sort /tmp/ahk_dc_dialog_out.txt 2>/dev/null) <(sort assert_dialog_expect.txt) | head -8
  fi
done
echo "dialog_repeat_done=1"
