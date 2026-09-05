#!/bin/bash
# Static guard for inputd frame-loss and per-device state recovery.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/source/linux/inputd/inputd.c"
grep -q 'SYN_DROPPED' "$SRC"
grep -q 'sPhysicalTxn\[MAX_DEVICES\]' "$SRC"
grep -q 'clear_device_physical_state' "$SRC"
grep -q 'device_has_held_keys' "$SRC"
grep -q 'sPhysicalTxn\[i\]\[code\]' "$SRC"
echo 'INPUTD_STATE_STATIC_PASS syn_dropped=1 per_device_state=1 held_reconcile=1'
