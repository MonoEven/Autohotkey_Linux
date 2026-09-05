#!/bin/bash
# Source-level guard for all production Linux D-Bus calls (check0905 P1).
# Every backend must use the shared bounded pending-call helper; no backend may
# reintroduce libdbus's blocking send_with_reply_and_block API.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
CORE="$ROOT/source/linux/core"
if grep -R -n --include='*.cpp' 'send_with_reply_and_block' "$CORE"; then
  echo 'COM_DBUS_STATIC_FAIL blocking API found in production Linux backend'
  exit 1
fi
grep -q 'dbus_connection_send_with_reply' "$CORE/core_dbus_call_linux.cpp"
grep -q 'DBusPendingCall' "$CORE/core_dbus_call_linux.cpp"
grep -q 'dbus_pending_call_get_completed' "$CORE/core_dbus_call_linux.cpp"
grep -q 'dbus_pending_call_cancel' "$CORE/core_dbus_call_linux.cpp"
grep -q 'MsgSleep(0, RETURN_AFTER_MESSAGES_SPECIAL_FILTER)' "$CORE/core_dbus_call_linux.cpp"
echo 'COM_DBUS_STATIC_PASS blocking=0 all_backends=1 pending=1 cancel=1 pump=1'
