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

VER="${1:-2.0.26-linux.9}"
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
install -m 0755 tools/linux/install.sh   "$STAGE/tools/linux/install.sh"
install -m 0755 tools/linux/install-gui.sh "$STAGE/tools/linux/install-gui.sh"
cp -r docs-v2 "$STAGE/docs-v2"
install -m 0644 README.md "$STAGE/README.md"
if [ -f LICENSE ]; then
  install -m 0644 LICENSE "$STAGE/LICENSE"
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
           "$DEBROOT/usr/share/doc/autohotkey" "$DEBROOT/DEBIAN"
  chmod 0755 "$DEBROOT" "$DEBROOT/DEBIAN"
  install -m 0755 "$CORE" "$DEBROOT/usr/share/autohotkey/ahk_core"
  cp -r docs-v2 "$DEBROOT/usr/share/doc/autohotkey/docs-v2"
  install -m 0644 README.md "$DEBROOT/usr/share/doc/autohotkey/README.md"
  [ -f LICENSE ] && install -m 0644 LICENSE "$DEBROOT/usr/share/doc/autohotkey/LICENSE"
  cat > "$DEBROOT/usr/bin/ahk" <<EOF
#!/bin/sh
exec /usr/share/autohotkey/ahk_core "\$@"
EOF
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
Description: AutoHotkey v2 Linux port (X11/Wayland)
 AutoHotkey is a free, open source macro-creation and automation
 utility driven by a custom scripting language with special provision
 for defining keyboard shortcuts (hotkeys).
 .
 This package provides the Linux port of AutoHotkey v2.0.26: a full X11
 backend (window management, controls, hotkeys, pixel access, dialogs),
 GTK3-based Gui/GuiControl/Menu support, D-Bus COM support, and a native
 Wayland backend (xdg-shell windows, virtual keyboard and pointer,
 wlr-screencopy).  AutoHotkey v2 syntax only; v1 is not supported.
 Documentation is under /usr/share/doc/autohotkey/docs-v2.
EOF
  cat > "$DEBROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-alternatives >/dev/null 2>&1; then
  update-alternatives --install /usr/bin/autohotkey autohotkey /usr/bin/ahk 50 \
    >/dev/null 2>&1 || true
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
  DEB="dist/autohotkey-linux-$VER-$ARCH.deb"
  ( cd "$(dirname "$DEBROOT")" && dpkg-deb --build --root-owner-group "$(basename "$DEBROOT")" "$REPO_DIR/$DEB" )
  rm -rf "$DEBROOT"
  echo "built: $DEB"
fi

echo
echo "Packages in ./dist/"
ls -la dist/*.tar.gz dist/*.deb 2>/dev/null
