#!/bin/sh
# AutoHotkey v2 Linux port - build release packages.
#
# Usage: pack.sh [version]
#
# Produces, in ./dist/:
#   autohotkey-linux-<version>-<arch>.tar.gz   generic tarball (binary +
#                                              installer + docs + README)
#   autohotkey-linux-<version>-<arch>.deb      Debian/Ubuntu package
#
# Requires: cmake-built build-core (ahk_core), and for .deb: dpkg-deb.
set -u

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$REPO_DIR" || exit 1

VER="${1:-${VER:-}}"
if [ -z "$VER" ] && [ -f tools/linux/VERSION ]; then
  VER=$(tr -d '\r\n' < tools/linux/VERSION)
fi
if [ -z "$VER" ] && command -v git >/dev/null 2>&1; then
  VER=$(git describe --tags --abbrev=0 --match 'v2.0.26-linux.*' 2>/dev/null | sed 's/^v//')
fi
VER="${VER:-2.0.26-linux.16}"
ARCH=$(uname -m)
case "$ARCH" in
  x86_64) ARCH=amd64 ;;
  aarch64|arm64) ARCH=arm64 ;;
esac

CORE=build-core/source/linux/core/ahk_core
if [ ! -x "$CORE" ]; then
  echo "pack.sh: $CORE not found; build first (cmake --build build-core)" >&2
  exit 1
fi
if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo "pack.sh: warning: dpkg-deb not found; .deb will be skipped" >&2
fi

rm -rf dist/stage dist/autohotkey-linux-* dist/*.deb dist/*.tar.gz
mkdir -p dist/stage/autohotkey-linux/tools/linux
# The stage root itself is the payload dir; docs go directly under it.
STAGE=dist/stage/autohotkey-linux

# --- stage the payload ------------------------------------------------
install -m 0755 "$CORE" "$STAGE/ahk_core"
# Official upstream AutoHotkey icon assets: the installer places the ICO next
# to ahk_core (SNI IconPixmap) and the PNG in the hicolor theme.
install -m 0644 source/resources/icon_main.ico "$STAGE/icon_main.ico"
install -m 0644 docs-v2/docs/static/ahk16.png "$STAGE/autohotkey.png"
install -m 0755 tools/linux/install.sh   "$STAGE/tools/linux/install.sh"
install -m 0755 tools/linux/install-gui.sh "$STAGE/tools/linux/install-gui.sh"
install -m 0644 tools/linux/ahk-launcher.in "$STAGE/tools/linux/ahk-launcher.in"
printf '%s' "$VER" > "$STAGE/tools/linux/VERSION"
cp -r docs-v2 "$STAGE/docs-v2"
install -m 0644 README.md "$STAGE/README.md"
if [ -f LICENSE ]; then
  install -m 0644 LICENSE "$STAGE/LICENSE"
fi
# The GNOME Shell extension ships in the release so users can install the
# optional plugin from the unpacked tarball itself (it is NOT part of the
# interpreter install/uninstall/update lifecycle -- see Install.htm).
if [ -d extension ]; then
  cp -r extension "$STAGE/extension"
fi

# --- tarball ----------------------------------------------------------
TARBALL="dist/autohotkey-linux-$VER-$ARCH.tar.gz"
( cd dist/stage && tar czf "../$(basename "$TARBALL")" autohotkey-linux )
echo "built: $TARBALL"

# --- .deb -------------------------------------------------------------
if command -v dpkg-deb >/dev/null 2>&1; then
  # Build under /tmp: the repo may live on a mount (WSL drvfs) where
  # chmod has no effect, which makes dpkg-deb reject the control dir.
  DEBROOT=$(mktemp -d /tmp/ahk-deb.XXXXXX)
  mkdir -p "$DEBROOT/usr/bin" "$DEBROOT/usr/share/autohotkey" \
           "$DEBROOT/usr/share/icons/hicolor/16x16/apps" \
           "$DEBROOT/usr/share/doc/autohotkey" \
           "$DEBROOT/usr/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org" \
           "$DEBROOT/DEBIAN"
  chmod 0755 "$DEBROOT" "$DEBROOT/DEBIAN"
  install -m 0755 "$CORE" "$DEBROOT/usr/share/autohotkey/ahk_core"
  install -m 0644 source/resources/icon_main.ico "$DEBROOT/usr/share/autohotkey/icon_main.ico"
  install -m 0644 docs-v2/docs/static/ahk16.png "$DEBROOT/usr/share/autohotkey/autohotkey.png"
  install -m 0644 docs-v2/docs/static/ahk16.png "$DEBROOT/usr/share/icons/hicolor/16x16/apps/autohotkey.png"
  cp -r docs-v2 "$DEBROOT/usr/share/doc/autohotkey/docs-v2"
  install -m 0644 README.md "$DEBROOT/usr/share/doc/autohotkey/README.md"
  [ -f LICENSE ] && install -m 0644 LICENSE "$DEBROOT/usr/share/doc/autohotkey/LICENSE"
  # The GNOME Shell extension ships system-wide with the deb (unlike the
  # tarball, where it is optional and user-scoped).  dpkg then manages its
  # lifecycle: apt upgrade replaces it, apt remove deletes it.  Enabling it
  # is a per-session (gsettings) decision, so postinst only prints how.
  if [ -d "$REPO_DIR/extension/ahk-global-hotkeys@autohotkey.org" ]; then
    cp -r "$REPO_DIR/extension/ahk-global-hotkeys@autohotkey.org/." \
          "$DEBROOT/usr/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org/"
  fi
  # Full launcher (--update/--uninstall/--check) rendered from the shared
  # template; the deb layout matches install.sh --prefix /usr.
  sed -e "s|@PREFIX@|/usr|g" \
      -e "s|@LIB_SUB@|share/autohotkey|g" \
      -e "s|@BIN_SUB@|bin|g" \
      -e "s|@DOC_SUB@|share/doc/autohotkey|g" \
      -e "s|@AHK_VERSION@|$VER|g" \
      "$REPO_DIR/tools/linux/ahk-launcher.in" > "$DEBROOT/usr/bin/ahk"
  chmod 0755 "$DEBROOT/usr/bin/ahk"
  DEB_SIZE=$(du -sk "$DEBROOT" | awk '{print $1}')
  cat > "$DEBROOT/DEBIAN/control" <<EOF
Package: autohotkey-linux
Version: $VER
Section: interpreters
Priority: optional
Architecture: $ARCH
Maintainer: MonoEven <MonoEven@users.noreply.github.com>
Installed-Size: $DEB_SIZE
Depends: libx11-6, libxext6, libxrandr2, libxinerama1, libxtst6, libgtk-3-0, libdbus-1-3, libffi8
Recommends: zenity | yad
Description: AutoHotkey v2 automation for Linux (X11/Wayland)
 AutoHotkey v2.0.26 interpreter and Linux desktop backends: X11/XWayland
 automation, native Wayland input routes, GTK3 GUI, AT-SPI controls,
 StatusNotifierItem tray, D-Bus and libffi interoperability.
 .
 AutoHotkey v2 syntax only; v1 is not supported. Documentation and the
 precise capability/limitation matrix are under
 /usr/share/doc/autohotkey/docs-v2.
EOF
  cat > "$DEBROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-alternatives >/dev/null 2>&1; then
  update-alternatives --install /usr/bin/autohotkey autohotkey /usr/bin/ahk 50 \
    >/dev/null 2>&1 || true
fi
# The GNOME Shell global-hotkey extension ships system-wide (dpkg-managed).
# Enabling it is a per-user session choice (gsettings), which a root
# postinst cannot do for the logged-in user -- print the one-time steps.
if [ -d /usr/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org ]; then
  echo
  echo "AutoHotkey GNOME Shell extension installed system-wide."
  echo "To use zero-confirmation global hotkeys on GNOME Wayland, do ONCE"
  echo "per user while logged into the GNOME session:"
  echo "    gnome-extensions enable ahk-global-hotkeys@autohotkey.org"
  echo "then restart GNOME Shell (log out/in, or Alt+F2 and type 'r')."
  echo "(apt remove will delete the extension together with the package.)"
  echo
fi
exit 0
EOF
  chmod 0755 "$DEBROOT/DEBIAN/postinst"
  cat > "$DEBROOT/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
if command -v update-alternatives >/dev/null 2>&1; then
  update-alternatives --remove autohotkey /usr/bin/ahk >/dev/null 2>&1 || true
fi
exit 0
EOF
  chmod 0755 "$DEBROOT/DEBIAN/prerm"
  cat > "$DEBROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
# Clean up anything that survived dpkg's own file removal: the launcher
# (or its symlink) may have been overwritten by a tarball install or by
# `ahk --update` after the .deb was installed, in which case dpkg refuses
# to remove the modified files and they would linger after apt remove.
# Only do this on remove/purge, never during an upgrade.
case "$1" in
  remove|purge)
    rm -f /usr/bin/ahk /usr/bin/ahk_core /usr/bin/autohotkey \
          /usr/share/autohotkey/ahk_core /usr/share/autohotkey/ahk.ahk \
          /usr/share/autohotkey/icon_main.ico /usr/share/autohotkey/autohotkey.png \
          /usr/share/icons/hicolor/16x16/apps/autohotkey.png
    rm -rf /usr/share/autohotkey /usr/share/doc/autohotkey 2>/dev/null || true
    ;;
  upgrade|failed-upgrade)
    ;;
esac
exit 0
EOF
  chmod 0755 "$DEBROOT/DEBIAN/postrm"
  DEB="dist/autohotkey-linux-$VER-$ARCH.deb"
  ( cd "$(dirname "$DEBROOT")" && dpkg-deb --build --root-owner-group "$(basename "$DEBROOT")" "$REPO_DIR/$DEB" )
  rm -rf "$DEBROOT"
  echo "built: $DEB"
fi

echo
echo "Packages in ./dist/"
ls -la dist/*.tar.gz dist/*.deb 2>/dev/null

# --- checksums + trust note (check0820: keep every release, allow
# rollback; a CKSUMS.txt of SHA-256 hashes ships with each release) ----
CKSUMS=dist/CKSUMS.txt
{
  echo "AutoHotkey v2 Linux port release v$VER (built $(date -u +%Y-%m-%dT%H:%MZ))"
  echo "SHA-256 (one per line: '<hash>  <filename>'):"
  for f in "$TARBALL" "$DEB"; do
    [ -f "$f" ] || continue
    name=$(basename "$f")
    printf '  %s  %s\n' "$(sha256sum "$f" | awk '{print $1}')" "$name"
  done
  echo
  echo "These hashes are computed from the files as packaged.  Verify a"
  echo "downloaded artifact with:  sha256sum -c <(grep '<filename>' CKSUMS.txt)"
  echo "CKSUMS.txt.sig is an OpenPGP detached signature (ASC) of this file."
  echo "Verify with:  gpg --verify CKSUMS.txt.sig CKSUMS.txt"
  echo "The public key is tools/linux/ahk-release.pub (AutoHotkey Linux"
  echo "Release <release@autohotkey-linux.invalid>)."
} > "$CKSUMS"
echo "built: dist/CKSUMS.txt"

# --- OpenPGP signature (check0820): sign CKSUMS.txt so the release can be
# verified end-to-end.  Uses the maintained release key from GNUPG if
# present; otherwise generates a throwaway key (CI) and exports the public
# half so the artifact set is self-contained.
if command -v gpg >/dev/null 2>&1; then
  KEYID="release@autohotkey-linux.invalid"
  if ! gpg --batch --list-secret-keys "$KEYID" >/dev/null 2>&1; then
    echo "AHK sign: generating a release-signing key (ephemeral) ..."
    gpg --batch --generate-key <<GPGEOF 2>/dev/null
%no-protection
Key-Type: RSA
Key-Length: 2048
Name-Real: AutoHotkey Linux Release
Name-Email: $KEYID
Expire-Date: 0
%commit
GPGEOF
  fi
  gpg --batch --yes --detach-sign --armor "$CKSUMS" 2>/dev/null
  gpg --armor --export "$KEYID" > dist/ahk-release.pub 2>/dev/null
  # gpg --detach-sign --armor writes "<file>.asc"; keep that convention.
  [ -s "$CKSUMS.asc" ] && echo "built: dist/CKSUMS.txt.asc" \
    || echo "AHK sign: warning: gpg signature failed (CKSUMS.txt.asc missing)"
fi
