#!/bin/bash
# Static guard for --pack failure safety (Audit47 I08).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/source/linux/core/core_pack_linux.cpp"
grep -q 'PackedResource' "$SRC"
grep -q 'cannot read FileInstall resource' "$SRC"
grep -q 'rename(temp_path.c_str(), aOut)' "$SRC"
grep -q 'slen > file_size - footer_size' "$SRC"
grep -q 'fsync(out)' "$SRC"
! grep -q 'res_data\[i\]' "$SRC"
echo PACK_STATIC_PASS resource_tuple=1 checked_footer=1 atomic_output=1
