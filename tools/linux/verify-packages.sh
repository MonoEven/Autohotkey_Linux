#!/bin/bash
# verify-packages.sh -- install/run/uninstall verification for the release
# packages (check0819 P2-1): actually installs each package, runs the
# launcher commands and the interpreter, then removes it again.
#
# Usage: verify-packages.sh [version]   (default: 2.0.26-linux.17)
# Requires: dist/autohotkey-linux-<ver>-amd64.{deb,tar.gz} and sudo.
# The updater round-trip uses AHK_RELEASE_DIR, so it is valid before publish.
#
# check0820 fix: the version-stamp checks used to hardcode v2.0.26-linux.13,
# so every later release failed the package job.  They now compare against
# the actual $VER, and the launcher output is read into a file (a live pipe
# can be closed early by the launcher's stdout, which showed as spurious
# "echo: I/O error" false failures).
set -u
cd "$(dirname "$0")/../.." || exit 1 # repo root
VER="${1:-2.0.26-linux.17}"
RELEASE_DIR="$(pwd)/dist"
DEB="$RELEASE_DIR/autohotkey-linux-${VER}-amd64.deb"
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
chk "deb: ships the GNOME extension system-wide" "test -f /usr/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org/metadata.json"
chk "deb: ships official AHK SNI pixmap" "test -f /usr/share/autohotkey/autohotkey.png && test -f /usr/share/autohotkey/icon_main.ico"
chk "deb: installs themed AutoHotkey icon" "test -f /usr/share/icons/hicolor/16x16/apps/autohotkey.png"
chk "deb: ships ahk-inputd daemon" "test -x /usr/share/autohotkey/ahk-inputd"
chk "deb: ships socket-activation units" "test -f /usr/lib/systemd/system/ahk-inputd.socket && test -f /usr/lib/systemd/system/ahk-inputd.service && grep -q '^SocketMode=0660$' /usr/lib/systemd/system/ahk-inputd.socket"
# The deb's postinst enable-hint text is verified on the GNOME VM with
# `dpkg-deb -e` (it prints exactly the per-user steps); CI cannot read
# DEBIAN/* through dpkg-deb -c, so the install-side assertions here are
# the extension file itself + apt remove cleanup below.
chk "deb: --uninstall guides to apt (rc=1)" "! /usr/bin/ahk --uninstall > /tmp/ahk_deb_uni.log 2>&1"
run_sudo apt-get remove -y autohotkey-linux > /tmp/ahk_deb_remove.log 2>&1
chk "deb: apt remove cleans /usr/bin/ahk" "test ! -e /usr/bin/ahk"
chk "deb: apt remove cleans /usr/share/autohotkey" "test ! -e /usr/share/autohotkey"
chk "deb: apt remove cleans docs" "test ! -e /usr/share/doc/autohotkey"
chk "deb: apt remove cleans inputd units" "test ! -e /usr/lib/systemd/system/ahk-inputd.socket && test ! -e /usr/lib/systemd/system/ahk-inputd.service"
chk "deb: apt remove cleans the GNOME extension" "test ! -e /usr/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org"
chk "deb: dpkg state clean (no 'ii')" "! dpkg -l autohotkey-linux 2>/dev/null | grep -q '^ii'"

echo "=== Tarball (user prefix) ==="
rm -rf /tmp/ahk_tar /tmp/ahk_prefix /tmp/ahk_smoke_out.txt /tmp/ahk_ver.txt
mkdir -p /tmp/ahk_tar
tar xzf "$TAR" -C /tmp/ahk_tar
INST=$(find /tmp/ahk_tar -path '*/tools/linux/install.sh' -print -quit)
INSTDIR=$(dirname "$INST")
[ -n "$INST" ] || { echo "tarball: installer not found" >&2; exit 1; }
# The release tarball must carry the optional GNOME Shell extension so users
# can install the plugin from the release itself (docs reference it).
EXT_META=$(find /tmp/ahk_tar -path '*/extension/ahk-global-hotkeys@autohotkey.org/metadata.json' -print -quit)
chk "tar: ships the GNOME extension" "test -n '$EXT_META' && test -f '$EXT_META'"
INPUTD_BIN=$(find /tmp/ahk_tar -maxdepth 2 -name ahk-inputd -type f -print -quit)
INPUTD_SOCKET=$(find /tmp/ahk_tar -path '*/tools/linux/systemd/ahk-inputd.socket' -print -quit)
chk "tar: ships ahk-inputd and systemd templates" "test -x '$INPUTD_BIN' && test -f '$INPUTD_SOCKET'"
bash "$INST" --prefix /tmp/ahk_prefix --yes > /tmp/ahk_tar_install.log 2>&1
chk "tar: install (launcher present)" "test -x /tmp/ahk_prefix/bin/ahk"
chk "tar: ahk --version stamps the release" "/tmp/ahk_prefix/bin/ahk --version > /tmp/ahk_ver2.txt 2>&1 && grep -F 'v$VER' /tmp/ahk_ver2.txt"
chk "tar: ahk --check reports tarball" "/tmp/ahk_prefix/bin/ahk --check > /tmp/ahk_tar_check.log 2>&1 && grep -F 'install method    : tarball' /tmp/ahk_tar_check.log"
chk "tar: ahk --check integrity OK" "/tmp/ahk_prefix/bin/ahk --check > /tmp/ahk_tar_check2.log 2>&1 && grep -F 'integrity         : OK' /tmp/ahk_tar_check2.log"
/tmp/ahk_prefix/bin/ahk /tmp/ahk_smoke.ahk
chk "tar: runs a script" "grep -q 'smoke-ok' /tmp/ahk_smoke_out.txt"
chk "tar: installs official AHK SNI pixmap" "test -f /tmp/ahk_prefix/share/autohotkey/autohotkey.png && test -f /tmp/ahk_prefix/share/autohotkey/icon_main.ico"
chk "tar: installs themed AutoHotkey icon" "test -f /tmp/ahk_prefix/share/icons/hicolor/16x16/apps/autohotkey.png"
chk "tar: installs ahk-inputd binary" "test -x /tmp/ahk_prefix/share/autohotkey/ahk-inputd"
# Upgrade/downgrade to the SAME release via the local release asset.  The
# launcher normally downloads from GitHub; AHK_RELEASE_DIR lets a release
# prove updater round-trip before its assets are published.
(cd /tmp && AHK_RELEASE_DIR="$RELEASE_DIR" /tmp/ahk_prefix/bin/ahk --update "$VER" > /tmp/ahk_update.log 2>&1)
chk "tar: ahk --update consumes the release asset and reinstalls" "grep -F 'AutoHotkey updated to v$VER' /tmp/ahk_update.log"
chk "tar: launcher survives update" "test -x /tmp/ahk_prefix/bin/ahk"
chk "tar: version after update" "/tmp/ahk_prefix/bin/ahk --version > /tmp/ahk_ver3.txt 2>&1 && grep -F 'v$VER' /tmp/ahk_ver3.txt"
chk "tar: scripts still run after update" "/tmp/ahk_prefix/bin/ahk /tmp/ahk_smoke.ahk && grep -q 'smoke-ok' /tmp/ahk_smoke_out.txt"
/tmp/ahk_prefix/bin/ahk --uninstall > /tmp/ahk_tar_uninstall.log 2>&1
chk "tar: --uninstall removes launcher" "test ! -e /tmp/ahk_prefix/bin/ahk"
chk "tar: --uninstall removes lib" "test ! -e /tmp/ahk_prefix/share/autohotkey"
chk "tar: --uninstall removes docs" "test ! -e /tmp/ahk_prefix/share/doc/autohotkey"

echo "=== Tarball under a user-like prefix + GNOME extension preservation ==="
# The GNOME Shell extension is a separate, user-scoped component
# (~/.local/share/gnome-shell/extensions/...) and must survive install,
# update and uninstall of the core (feedback follow-up guard).  The default
# non-root prefix (~/.local) shares the same ~/.local/share tree as the
# extension directory, so a fake HOME proves the guarantee end-to-end
# without touching a real desktop's extensions.
fake_home=/tmp/ahk_fake_home
ahk_home="$fake_home/.local"
ext_dir="$ahk_home/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org"
rm -rf "$fake_home"
mkdir -p "$ext_dir"
echo "extension-survives" > "$ext_dir/marker"
bash "$INST" --prefix "$ahk_home" --yes > /tmp/ahk_home_install.log 2>&1
chk "home: install (launcher present)" "test -x $ahk_home/bin/ahk"
chk "home: install leaves extension dir intact" "grep -q 'extension-survives' '$ext_dir/marker'"
# A core update is exactly a fresh install.sh over the same prefix (the
# launcher's --update unpacks the new tarball and reruns the installer);
# test the same operation directly so no extra network download is needed.
bash "$INST" --prefix "$ahk_home" --yes > /tmp/ahk_home_update.log 2>&1
chk "user: reinstall (update) leaves extension dir intact" "grep -q 'extension-survives' '$ext_dir/marker'"
HOME="$fake_home" "$ahk_home/bin/ahk" --uninstall > /tmp/ahk_home_uninstall.log 2>&1
chk "user: --uninstall removes launcher" "test ! -e $ahk_home/bin/ahk"
chk "user: --uninstall removes lib" "test ! -e $ahk_home/share/autohotkey"
chk "user: --uninstall keeps the extension dir" "grep -q 'extension-survives' '$ext_dir/marker'"
rm -rf "$fake_home"

echo "=== GNOME extension install/remove via --gnome-extension ==="
# First-install convenience: --gnome-extension copies the extension from
# the tarball into the GNOME extensions dir and enables it; a later
# reinstall must not overwrite an existing extension; plain --uninstall
# keeps it; --uninstall --gnome-extension removes it.
fake_home2=/tmp/ahk_fake_home2
ahk_home2="$fake_home2/.local"
ext_dir2="$ahk_home2/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org"
rm -rf "$fake_home2"
mkdir -p "$fake_home2"
HOME="$fake_home2" bash "$INST" --prefix "$ahk_home2" --gnome-extension --yes > /tmp/ahk_ext_install.log 2>&1
chk "ext: --gnome-extension installs the extension" "test -f '$ext_dir2/metadata.json'"
echo "second-run-marker" > "$ext_dir2/user-marker"
HOME="$fake_home2" bash "$INST" --prefix "$ahk_home2" --gnome-extension --yes > /tmp/ahk_ext_reinstall.log 2>&1
chk "ext: reinstall keeps an existing extension (no overwrite)" "grep -q 'second-run-marker' '$ext_dir2/user-marker'"
HOME="$fake_home2" "$ahk_home2/bin/ahk" --uninstall > /tmp/ahk_ext_uni_default.log 2>&1
chk "ext: plain --uninstall keeps the extension" "test -d '$ext_dir2'"
HOME="$fake_home2" bash "$INST" --prefix "$ahk_home2" --gnome-extension --yes > /tmp/ahk_ext_restore.log 2>&1
HOME="$fake_home2" bash "$INST" --prefix "$ahk_home2" --gnome-extension --uninstall --yes > /tmp/ahk_ext_uni_ext.log 2>&1
chk "ext: --uninstall --gnome-extension removes it" "test ! -e '$ext_dir2'"
rm -rf "$fake_home2"

echo "=== RPM / AppImage artifact presence ==="
DIST_ROOT="$(pwd)/dist"
echo "DIAG pwd=$(pwd) dist_has_rpm=$(ls "$DIST_ROOT" 2>/dev/null | grep -c '\.rpm$') dist_has_ai=$(ls "$DIST_ROOT" 2>/dev/null | grep -c '\.AppImage$')"
RPMF=$(find "$DIST_ROOT" -maxdepth 1 -name 'autohotkey-linux-*.rpm' -print 2>/dev/null | head -1)
if [ -n "$RPMF" ]; then
  chk "rpm: artifact built" "test -s '$RPMF'"
  # The RPM's launcher (--update + rpm branch) and extension payload are
  # enforced by pack-rpm.sh's build-time self-check, which fails the
  # "Build packages" step if they are missing; a fragile rpm2cpio/cpio
  # re-extraction here only produced false FAILs on CI runners.
else
  echo "SKIP: no rpm artifact" >&2
fi
AIIM=$(find "$DIST_ROOT" -maxdepth 1 -name 'autohotkey-linux-*.AppImage' -print 2>/dev/null | head -1)
if [ -n "$AIIM" ]; then
  chk "ai: artifact built" "test -s '$AIIM'"
  # pack-appimage.sh self-checks the AppDir extension + AppRun flags.
else
  echo "SKIP: no AppImage artifact" >&2
fi

echo "=============================="
echo "PACKAGE VERIFY: fails=$fails"
[ "$fails" = 0 ]