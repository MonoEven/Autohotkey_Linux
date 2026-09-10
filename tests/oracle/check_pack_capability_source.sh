#!/bin/bash
# Static guard for the packed-runtime capability manifest (Audit47 §13.2).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PACK="$ROOT/source/linux/core/core_pack_linux.cpp"
HDR="$ROOT/source/linux/core/core_pack_linux.h"
MAIN="$ROOT/source/linux/core/main_linux.cpp"
CMAKE="$ROOT/source/linux/core/CMakeLists.txt"

# The manifest exists, uses a reserved name that cannot collide with a real
# FileInstall source path, and is embedded rather than synthesized at run time.
grep -q 'AHK_PACK_MANIFEST_NAME' "$HDR"
grep -q 'ahk-pack-manifest' "$HDR"
grep -q 'LinuxPackManifestText' "$PACK"
grep -q 'LinuxPackParseManifest' "$PACK"
grep -q 'LinuxPackReadManifest' "$PACK"

# The template is probed, and an unprobeable template is an explicit error.
grep -q 'LinuxPackProbeRuntime' "$PACK"
grep -q 'cannot determine the capabilities of ' "$PACK"
grep -q 'the pack template ' "$PACK"

# The capability-loss notice must name the lane and how to observe it.
grep -q 'libei/EIS injection but the pack template was not' "$PACK"
grep -q 'HotkeyBackendGet().libei_build_enabled' "$PACK"
! grep -q 'AHK_PACK_ASSUME_TEMPLATE' "$PACK"

# The runtime exposes its own facts, and the release string comes from the one
# version source shared with the packaging scripts.
grep -q '"--pack-info"' "$MAIN"
grep -q 'LinuxPackSelfInfo' "$MAIN"
grep -q 'AHK_LINUX_RELEASE_VERSION' "$CMAKE"
grep -q 'tools/linux/VERSION' "$CMAKE"

# A packed process is compiled for its whole lifetime, and its arguments reach
# the embedded script.
grep -q 'g_LinuxPacked = LinuxIsPacked();' "$MAIN"
grep -q 'packed_args_start' "$MAIN"
grep -q '"/script"' "$MAIN"

echo 'PACK_CAPABILITY_STATIC_PASS manifest=1 probe=1 notice=1 packinfo=1 args=1'
