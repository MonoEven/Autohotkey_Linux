#!/bin/sh
# AutoHotkey v2 Linux port - AppImage builder.
#
# Usage: pack-appimage.sh [version]
#
# Produces dist/autohotkey-linux-<version>-<arch>.AppImage using
# appimagetool (downloaded on demand from GitHub releases).
set -u

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$REPO_DIR" || exit 1

VER="${1:-2.0.26-linux.3}"
ARCH=$(uname -m)
case "$ARCH" in
  x86_64) ARCH=x86_64 ;;
  aarch64|arm64) ARCH=aarch64 ;;
esac

CORE=build-core/source/linux/core/ahk_core
if [ ! -x "$CORE" ]; then
  echo "pack-appimage.sh: $CORE not found; build first" >&2
  exit 1
fi

# Fetch appimagetool if needed.
TOOL=/tmp/appimagetool
if [ ! -x "$TOOL" ]; then
  echo "downloading appimagetool ..."
  curl -fsSL -o /tmp/appimagetool.gz \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage.gz" \
    || { echo "failed to download appimagetool" >&2; exit 1; }
  gunzip -f /tmp/appimagetool.gz
  chmod +x /tmp/appimagetool
fi

APP=dist/appimage/autohotkey.AppDir
rm -rf dist/appimage
mkdir -p "$APP/usr/bin" "$APP/usr/share/applications" \
         "$APP/usr/share/icons/hicolor/scalable/apps"

install -m 0755 "$CORE" "$APP/usr/bin/ahk_core"
cat > "$APP/usr/bin/ahk" <<'EOF'
#!/bin/sh
SELF=$(readlink -f "$0")
DIR=$(dirname "$SELF")
exec "$DIR/ahk_core" "$@"
EOF
chmod 0755 "$APP/usr/bin/ahk"

cat > "$APP/autohotkey.desktop" <<EOF
[Desktop Entry]
Name=AutoHotkey (Linux)
Comment=Automation scripting utility (v2, X11/Wayland)
Exec=ahk
Type=Application
Categories=Utility;
Icon=autohotkey
Terminal=false
EOF
cp "$APP/autohotkey.desktop" "$APP/usr/share/applications/"

# Icon: reuse the official logo (SVG) from the docs mirror.
if [ -f docs-v2/docs/static/ahk_logo.svg ]; then
  cp docs-v2/docs/static/ahk_logo.svg \
     "$APP/usr/share/icons/hicolor/scalable/apps/autohotkey.svg"
fi

cat > "$APP/AppRun" <<'EOF'
#!/bin/sh
SELF=$(readlink -f "$0")
HERE=$(dirname "$SELF")
exec "$HERE/usr/bin/ahk_core" "$@"
EOF
chmod 0755 "$APP/AppRun"

cat > "$APP/.desktop" <<'EOF'
[Desktop Entry]
Name=AutoHotkey (Linux)
Exec=ahk
Type=Application
Icon=autohotkey
EOF

OUT="dist/autohotkey-linux-$VER-$ARCH.AppImage"
"$TOOL" "$APP" "$OUT" >/dev/null 2>&1 || {
  # Fallback: build without icon embedding on failure.
  "$TOOL" --no-appstream "$APP" "$OUT"
}
chmod +x "$OUT" 2>/dev/null || true
echo "built: $OUT"
ls -la "$OUT"
