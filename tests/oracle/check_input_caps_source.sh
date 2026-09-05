#!/bin/bash
# Static guard for backend capability contracts (check0905 P1/P2).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DEF="$ROOT/source/linux/core/input_caps.def"
HDR="$ROOT/source/linux/core/input_backend.h"
SRC="$ROOT/source/linux/core/input_backend.cpp"
# Registered accelerator APIs consume the shortcut but do not provide a
# hook-equivalent AHK suppression guarantee.
grep -A1 'AHK_INPUT_CAPS(PORTAL, "portal"' "$DEF" | grep -q 'true, true'
grep -A1 'AHK_INPUT_CAPS(GNOME_SHELL, "gnome-shell"' "$DEF" | grep -q 'true, true'
grep -q 'adapted_suppression' "$SRC"
grep -q 'aRequireSuppression' "$HDR"
grep -q 'if (aRequireSuppression && !c->suppress)' "$SRC"
grep -q '!passthrough);' "$SRC"
echo 'INPUT_CAPS_STATIC_PASS accelerator_suppression=adapted route_requirement=1'
