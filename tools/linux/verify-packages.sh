#!/bin/bash
# verify-packages.sh -- install/run/uninstall verification for the release
# packages (check0819 P2-1: the package CI used to only list archive
# contents and read .deb metadata; this actually installs each package,
# runs the interpreter and the launcher commands, then removes it again).
#
# Usage: verify-packages.sh [version]   (default: 2.0.26-linux.13)
# Requires: dist/autohotkey-linux-<ver>-amd64.{deb,tar.gz}, sudo, network
# (the tarball update step downloads the release asset from GitHub).
set -u
cd "$(dirname "$0")/../.." || exit 1 # repo root
VER="${1:-2.0.26-linux.13}"
# apt-get install of a local .deb requires a ./ or absolute path (a bare
# "dist/..." is parsed as a package name).
DEB="$(pwd)/dist/autohotkey-linux-${VER}-amd64.deb"
TAR="$(pwd)/dist/autohotkey-linux-${VER}-amd64.tar.gz"

[ -f "$DEB" ] || { echo "missing $DEB" >&2; exit 1; }
[ -f "$TAR" ] || { echo "missing $TAR" >&2; exit 1; }

fails=0
chk() { # name, shell-condition
  if sh -c "$2"; then
    echo "PASS: $1"
  else
    echo "FAIL: $1"
    fails=$((fails + 1))
  fi
}

# sudo wrapper: CI runners have passwordless sudo; local/VM runs can pass
# AHK_SUDO_PASSWORD (some systems use sudo-rs, which never caches).
run_sudo() {
  if [ -n "${AHK_SUDO_PASSWORD:-}" ]; then
    echo "$AHK_SUDO_PASSWORD" | sudo -S "$@" 2>/dev/null
  else
    sudo "$@"
  fi
}

cat > /tmp/ahk_smoke.ahk <<'EOF'
#Requires AutoHotkey v2.0
FileAppend("smoke-ok`n", "/tmp/ahk_smoke_out.txt")
ExitApp(0)
EOF

echo "=== Debian package ==="
rm -f /tmp/ahk_smoke_out.txt
run_sudo apt-get install -y --no-install-recommends "$DEB" > /tmp/ahk_deb_install.log 2>&1
chk "deb: install (ahk present)" "test -x /usr/bin/ahk"
chk "deb: ahk --version stamps the release" "/usr/bin/ahk --version | grep -q 'v2.0.26-linux.13'"
chk "deb: ahk --check reports dpkg" "/usr/bin/ahk --check | grep -q 'install method    : Debian/Ubuntu package'"
chk "deb: ahk --check integrity OK" "/usr/bin/ahk --check | grep -q 'integrity         : OK'"
/usr/bin/ahk /tmp/ahk_smoke.ahk
chk "deb: runs a script" "grep -q 'smoke-ok' /tmp/ahk_smoke_out.txt"
chk "deb: --uninstall guides to apt (rc=1)" "! /usr/bin/ahk --uninstall > /tmp/ahk_deb_uni.log 2>&1"
run_sudo apt-get remove -y autohotkey-linux > /tmp/ahk_deb_remove.log 2>&1
chk "deb: apt remove cleans /usr/bin/ahk" "test ! -e /usr/bin/ahk"
chk "deb: apt remove cleans /usr/share/autohotkey" "test ! -e /usr/share/autohotkey"
chk "deb: apt remove cleans docs" "test ! -e /usr/share/doc/autohotkey"
chk "deb: dpkg state clean (no 'ii')" "! dpkg -l autohotkey-linux 2>/dev/null | grep -q '^ii'"

echo "=== Tarball (user prefix) ==="
rm -rf /tmp/ahk_tar /tmp/ahk_prefix /tmp/ahk_smoke_out.txt
mkdir -p /tmp/ahk_tar
tar xzf "$TAR" -C /tmp/ahk_tar
INST=$(find /tmp/ahk_tar -path '*/tools/linux/install.sh' -print -quit)
INSTDIR=$(dirname "$INST")
[ -n "$INST" ] || { echo "tarball: installer not found" >&2; exit 1; }
bash "$INST" --prefix /tmp/ahk_prefix --yes > /tmp/ahk_tar_install.log 2>&1
chk "tar: install (launcher present)" "test -x /tmp/ahk_prefix/bin/ahk"
chk "tar: ahk --version stamps the release" "/tmp/ahk_prefix/bin/ahk --version | grep -q 'v2.0.26-linux.13'"
chk "tar: ahk --check reports tarball" "/tmp/ahk_prefix/bin/ahk --check | grep -q 'install method    : tarball'"
chk "tar: ahk --check integrity OK" "/tmp/ahk_prefix/bin/ahk --check | grep -q 'integrity         : OK'"
/tmp/ahk_prefix/bin/ahk /tmp/ahk_smoke.ahk
chk "tar: runs a script" "grep -q 'smoke-ok' /tmp/ahk_smoke_out.txt"
# Upgrade/downgrade to the SAME release via the GitHub asset (real network
# round-trip; also proves the launcher survives the reinstall).
(cd /tmp && /tmp/ahk_prefix/bin/ahk --update "$VER" > /tmp/ahk_update.log 2>&1)
chk "tar: ahk --update downloads and reinstalls" "grep -q 'AutoHotkey updated to v$VER' /tmp/ahk_update.log"
chk "tar: launcher survives update" "test -x /tmp/ahk_prefix/bin/ahk"
chk "tar: version after update" "/tmp/ahk_prefix/bin/ahk --version | grep -q 'v2.0.26-linux.13'"
chk "tar: scripts still run after update" "/tmp/ahk_prefix/bin/ahk /tmp/ahk_smoke.ahk && grep -q 'smoke-ok' /tmp/ahk_smoke_out.txt"
/tmp/ahk_prefix/bin/ahk --uninstall > /tmp/ahk_tar_uninstall.log 2>&1
chk "tar: --uninstall removes launcher" "test ! -e /tmp/ahk_prefix/bin/ahk"
chk "tar: --uninstall removes lib" "test ! -e /tmp/ahk_prefix/share/autohotkey"
chk "tar: --uninstall removes docs" "test ! -e /tmp/ahk_prefix/share/doc/autohotkey"

echo "=============================="
echo "PACKAGE VERIFY: fails=$fails"
[ "$fails" = 0 ]