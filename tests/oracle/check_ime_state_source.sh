#!/bin/bash
# Static guard for IME thread/lifecycle safety (check0905 P1/P2).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/source/linux/core/core_ime_linux.cpp"
grep -q 'recursive_mutex sImeListenerMutex' "$SRC"
grep -q 'lock_guard<std::recursive_mutex> listener_lock' "$SRC"
grep -q 'lock_guard<std::mutex> lock(sImeStateMutex)' "$SRC"
grep -q 'bool LinuxImePreeditActive' "$SRC"
grep -q 'LinuxDbusPendingReply' "$SRC"
! grep -q 'send_with_reply_and_block' "$SRC"
echo 'IME_STATE_STATIC_PASS listener_lifetime=1 state_lock=1 bounded_dbus=1'
