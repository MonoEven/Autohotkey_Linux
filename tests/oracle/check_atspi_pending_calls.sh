#!/bin/bash
# Source-level guard for the VM runtime pending-call oracle. GitHub runners do
# not provide a GNOME AT-SPI desktop, so CI additionally prevents accidental
# reintroduction of libdbus's blocking convenience API.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/source/linux/core/core_atspi_linux.cpp"
if grep -q 'send_with_reply_and_block' "$SRC"; then
  echo 'ATSPI_PENDING_STATIC_FAIL blocking API found'
  grep -n 'send_with_reply_and_block' "$SRC"
  exit 1
fi
grep -q 'dbus_connection_send_with_reply' "$SRC"
grep -q 'DBusPendingCall' "$SRC"
grep -q 'dbus_pending_call_get_completed' "$SRC"
grep -q 'MsgSleep(' "$SRC"
grep -q 'pending_calls=%lu pump_slices=%lu' "$SRC"
grep -q 'sPendingWaitDepth' "$SRC"
echo 'ATSPI_PENDING_STATIC_PASS blocking=0 pending=1 pump=1 reentry_guard=1'
