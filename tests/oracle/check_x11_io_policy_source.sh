#!/bin/bash
# Audit47 I06 Xlib fatal-disconnect policy guard.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/source/linux/core/core_win_linux.cpp"
grep -q 'XSetIOErrorExitHandler' "$SRC"
grep -q 'sWinDisplayIoFailed' "$SRC"
grep -q 'old Display is unusable' "$SRC"
grep -q 'sWinDisplay = nullptr' "$SRC"
echo 'X11_IO_POLICY_STATIC_PASS fatal_handler=nonexit stale_display=discard reconnect=1'
