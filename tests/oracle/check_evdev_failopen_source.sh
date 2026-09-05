#!/bin/bash
# Static guard for the in-process evdev fail-open contract (check0905 P0).
# A physical /dev/input + /dev/uinput oracle remains environment-specific;
# this guard prevents the safety checks from silently disappearing in CI.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
EV="$ROOT/source/linux/core/core_evdev_linux.cpp"
UI="$ROOT/source/linux/core/core_uinput_linux.cpp"
grep -q 'EVIOCGBIT(EV_KEY' "$EV"
grep -q 'DeviceHasKeyboardCapabilities' "$EV"
grep -q 'sLocalReplayAvailable' "$EV"
grep -q '!LinuxUinputKeyEvent' "$EV"
grep -q 'ReleaseLocalGrabsForReplayFailure' "$EV"
grep -q 'bool WriteUinputEvent' "$UI"
grep -q 'return WriteUinputEvent(EV_KEY' "$UI"
grep -q 'poll(&pfd, 1, 20)' "$UI"
echo 'EVDEV_FAILOPEN_STATIC_PASS keyboard_filter=1 replay_preflight=1 write_result=1 grab_release=1'
