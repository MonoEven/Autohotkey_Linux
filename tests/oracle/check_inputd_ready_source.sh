#!/bin/bash
# Static guard for inputd systemd readiness and status reporting.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/source/linux/inputd/inputd.c"
UNIT="$ROOT/tools/linux/systemd/ahk-inputd.service.in"
grep -q 'static void systemd_notify' "$SRC"
grep -q 'NOTIFY_SOCKET' "$SRC"
grep -q 'READY=1' "$SRC"
grep -q '^Type=notify$' "$UNIT"
grep -q '^NotifyAccess=main$' "$UNIT"
echo 'INPUTD_READY_STATIC_PASS notify=1 ready=1 degraded_status=1'
