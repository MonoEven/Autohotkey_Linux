#!/bin/bash
# verify-packages.sh -- install/run/uninstall verification for the release
# packages (check0819 P2-1): actually installs each package, runs the
# launcher commands and the interpreter, then removes it again.
#
# Usage: verify-packages.sh [version]   (default: 2.0.26-linux.13)
# Requires: dist/autohotkey-linux-<ver>-amd64.{deb,tar.gz}, sudo, network
# (the tarball update step downloads the release asset from GitHub).
#
# check0820 fix: the version-stamp checks used to hardcode v2.0.26-linux.13,
# so every later release failed the package job.  They now compare against
# the actual $VER, and the launcher output is read into a file (a live pipe
# can be closed early by the launcher's stdout, which showed as spurious
# "echo: I/O error" false failures).
set -u
cd "$(dirname "$0")/../.." || exit 1 # repo root
VER="${1:-2.0.26-linux.13}"
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
rm -f /tmp/ahk_smoke_out.txt /tmp/ahk_ver.txt
run_sudo apt-get install -y --no-install-recommends "$DEB" > /tmp/ahk_deb_install.log 2>&1
chk "deb: install (ahk present)" "test -x /usr/bin/ahk"
chk "deb: ahk --version stamps the release" "/usr/bin/ahk --version > /tmp/ahk_ver.txt 2>&1 && grep -q 'v$VER' /tmp/ahk_ver.txt"
chk "deb: ahk --check reports dpkg" "/usr/bin/ahk --check > /tmp/ahk_deb_check.log 2>&1 && grep -q 'install method    : Debian/Ubuntu package' /tmp/ahk_deb_check.log"
chk "deb: ahk --check integrity OK" "/usr/bin/ahk --check > /tmp/ahk_deb_check2.log 2>&1 && grep -q 'integrity         : OK' /tmp/ahk_deb_check2.log"
/usr/bin/ahk /tmp/ahk_smoke.ahk
chk "deb: runs a script" "grep -q 'smoke-ok' /tmp/ahk_smoke_out.txt"
chk "deb: --uninstall guides to apt (rc=1)" "! /usr/bin/ahk --uninstall > /tmp/ahk_deb_uni.log 2>&1"
run_sudo apt-get remove -y autohotkey-linux > /tmp/ahk_deb_remove.log 2>&1
chk "deb: apt remove cleans /usr/bin/ahk" "test ! -e /usr/bin/ahk"
chk "deb: apt remove cleans /usr/share/autohotkey" "test ! -e /usr/share/autohotkey"
chk "deb: apt remove cleans docs" "test ! -e /usr/share/doc/autohotkey"
chk "deb: dpkg state clean (no 'ii')" "! dpkg -l autohotkey-linux 2>/dev/null | grep -q '^ii'"

echo "=== Tarball (user prefix) ==="
rm -rf /tmp/ahk_tar /tmp/ahk_prefix /tmp/ahk_smoke_out.txt /tmp/ahk_ver.txt
mkdir -p /tmp/ahk_tar
tar xzf "$TAR" -C /tmp/ahk_tar
INST=$(find /tmp/ahk_tar -path '*/tools/linux/install.sh' -print -quit)
INSTDIR=$(dirname "$INST")
[ -n "$INST" ] || { echo "tarball: installer not found" >&2; exit 1; }
bash "$INST" --prefix /tmp/ahk_prefix --yes > /tmp/ahk_tar_install.log 2>&1
chk "tar: install (launcher present)" "test -x /tmp/ahk_prefix/bin/ahk"
chk "tar: ahk --version stamps the release" "/tmp/ahk_prefix/bin/ahk --version > /tmp/ahk_ver2.txt 2>&1 && grep -F 'v$VER' /tmp/ahk_ver2.txt"
chk "tar: ahk --check reports tarball" "/tmp/ahk_prefix/bin/ahk --check > /tmp/ahk_tar_check.log 2>&1 && grep -F 'install method    : tarball' /tmp/ahk_tar_check.log"
chk "tar: ahk --check integrity OK" "/tmp/ahk_prefix/bin/ahk --check > /tmp/ahk_tar_check2.log 2>&1 && grep -F 'integrity         : OK' /tmp/ahk_tar_check2.log"
/tmp/ahk_prefix/bin/ahk /tmp/ahk_smoke.ahk
chk "tar: runs a script" "grep -q 'smoke-ok' /tmp/ahk_smoke_out.txt"
# Upgrade/downgrade to the SAME release via the GitHub asset (real network
# round-trip; proves the launcher survives the reinstall and re-stamps).
(cd /tmp && /tmp/ahk_prefix/bin/ahk --update "$VER" > /tmp/ahk_update.log 2>&1)
chk "tar: ahk --update downloads and reinstalls" "grep -F 'AutoHotkey updated to v$VER' /tmp/ahk_update.log"
chk "tar: launcher survives update" "test -x /tmp/ahk_prefix/bin/ahk"
chk "tar: version after update" "/tmp/ahk_prefix/bin/ahk --version > /tmp/ahk_ver3.txt 2>&1 && grep -F 'v$VER' /tmp/ahk_ver3.txt"
chk "tar: scripts still run after update" "/tmp/ahk_prefix/bin/ahk /tmp/ahk_smoke.ahk && grep -q 'smoke-ok' /tmp/ahk_smoke_out.txt"
/tmp/ahk_prefix/bin/ahk --uninstall > /tmp/ahk_tar_uninstall.log 2>&1
chk "tar: --uninstall removes launcher" "test ! -e /tmp/ahk_prefix/bin/ahk"
chk "tar: --uninstall removes lib" "test ! -e /tmp/ahk_prefix/share/autohotkey"
chk "tar: --uninstall removes docs" "test ! -e /tmp/ahk_prefix/share/doc/autohotkey"

echo "=============================="
echo "PACKAGE VERIFY: fails=$fails"
[ "$fails" = 0 ]