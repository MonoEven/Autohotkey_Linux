#!/bin/bash
# Source-level guard for M7 D-Bus calls (check_detail0901 §10.1).
# Prevents accidental reintroduction of send_with_reply_and_block into the
# Linux COM layer and ensures bounded pending calls are maintained.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/source/linux/core/core_com_dbus_linux.cpp"
if grep -q 'send_with_reply_and_block' "$SRC"; then
  echo 'COM_DBUS_STATIC_FAIL blocking API found in core_com_dbus_linux.cpp'
  grep -n 'send_with_reply_and_block' "$SRC"
  exit 1
fi
grep -q 'dbus_connection_send_with_reply' "$SRC"
grep -q 'DBusPendingCall' "$SRC"
grep -q 'dbus_pending_call_get_completed' "$SRC"
grep -q 'dbus_pending_call_cancel' "$SRC"
grep -q 'MsgSleep(0, RETURN_AFTER_MESSAGES_SPECIAL_FILTER)' "$SRC"
grep -q 'AHK_COM_CALL_TIMEOUT_MS' "$SRC"
echo 'COM_DBUS_STATIC_PASS blocking=0 pending=1 cancel=1 pump=1 bounded_env=1'
