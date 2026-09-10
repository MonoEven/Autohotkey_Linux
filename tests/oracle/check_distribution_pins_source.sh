#!/bin/bash
# Audit47 I11 distribution pin and checksum guard.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PKG="$ROOT/tools/linux/PKGBUILD"
FLAT="$ROOT/tools/linux/org.autohotkey.AHK.yml"
grep -q 'pkgver=2.0.26.linux.23' "$PKG"
grep -q 'v2.0.26-linux.23.tar.gz' "$PKG"
grep -Eq "sha256sums=\('[0-9a-f]{64}'\)" "$PKG"
! grep -q "sha256sums=('SKIP')" "$PKG"
grep -q 'tag: v2.0.26-linux.23' "$FLAT"
grep -q -- '--device=input' "$FLAT"
grep -q '/dev/uinput' "$FLAT"
echo 'PACKAGE_PINS_STATIC_PASS aur=linux.23 checksum=1 flatpak=linux.23'
