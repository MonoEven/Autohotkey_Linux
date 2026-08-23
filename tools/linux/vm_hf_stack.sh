#!/bin/bash
# vm_hf_stack.sh -- try to get a stack of the hung process.
set -u
R=/tmp/xtest_hf_stack.txt
exec > "$R" 2>&1
PID=$(pgrep -f 'hf_diag.ahk' | head -1)
echo "hung pid: ${PID:-none}"
if [ -n "$PID" ]; then
  echo "--- /proc stack ---"
  cat /proc/$PID/stack 2>/dev/null | head -5 || echo "(no stack)"
  echo "--- gdb attach attempt ---"
  if command -v gdb >/dev/null 2>&1; then
    timeout 8 gdb -p "$PID" -batch -ex "thread apply all bt" 2>&1 | head -25
  else
    echo "(no gdb)"
  fi
fi
echo "hf_stack_done=1"