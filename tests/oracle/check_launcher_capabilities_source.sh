#!/bin/bash
# Audit47 I04 install-time diagnostics guard.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/tools/linux/ahk-launcher.in"
grep -q 'input backend' "$SRC"
grep -q 'inputd broker' "$SRC"
grep -q 'AHK_INPUTD_DISABLE' "$SRC"
grep -q '/dev/uinput' "$SRC"
grep -q 'grants negotiated at runtime' "$SRC"
echo 'LAUNCHER_CAPS_STATIC_PASS backend=1 broker=1 uinput=1 grant_boundary=1'
