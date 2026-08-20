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

VER="${1:-2.0.26-linux.6}"
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

# Fetch appimagetool if needed.  The release asset is a plain
# appimagetool-<arch>.AppImage (no .gz suffix since AppImageKit 2020; the
# old continuous .gz URL returns 404, check0820 fix).  appimagetool is
# itself an AppImage, so on machines without FUSE it is extracted first
# and the inner binary is used (works on CI runners and containers).
TOOL=/tmp/appimagetool
if [ ! -x "$TOOL" ]; then
  echo "downloading appimagetool ..."
  dl_tool() {
    curl -fsSL -o "$TOOL.tmp" \
      "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage" \
      || curl -fsSL -o "$TOOL.tmp" \
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-$ARCH.AppImage"
  }
  # Sanity-check the download (it must be an ELF AppImage, not a 404 page
  # or an empty body); retry once; GitHub-hosted runners have mainly seen
  # empty/truncated bodies here (check0820 round).
  dl_ok=0
  for attempt in 1 2; do
    if dl_tool 2>/dev/null && [ -s "$TOOL.tmp" ]; then
      magic_ok=$(python3 - <<'PY'
import sys
with open('/tmp/appimagetool.tmp', 'rb') as f:
    print('1' if f.read(4) == b'\x7fELF' else '0')
PY
)
      if [ "$magic_ok" = "1" ]; then
        mv "$TOOL.tmp" "$TOOL"
        dl_ok=1
        break
      fi
    fi
    echo "pack-appimage.sh: download attempt $attempt failed sanity check; retrying" >&2
  done
  rm -f "$TOOL.tmp"
  if [ "$dl_ok" -ne 1 ]; then
    echo "failed to download a valid appimagetool" >&2
    exit 1
  fi
  chmod +x "$TOOL"
fi
# Prefer the extracted inner binary (avoids FUSE; GitHub-hosted runners
# have no /dev/fuse, so the AppImage itself cannot be executed there).
TOOLBIN=/tmp/squashfs-root/usr/bin/appimagetool
OFFSET="-1"
if [ ! -x "$TOOLBIN" ]; then
  rm -rf /tmp/squashfs-root
  if ! ( cd /tmp && "$TOOL" --appimage-extract >/dev/null 2>&1 ); then
    # Fallback without the AppImage runtime at all: unsquashfs the
    # embedded squashfs directly.  The filesystem starts at the last
    # "hsqs" magic in the file (type-2 AppImage); locate it with
    # python3 (also used for the icon below).
    OFFSET=$(python3 - <<'PY'
import sys
data = open('/tmp/appimagetool', 'rb').read()
i = data.rfind(b'hsqs')
print(('%d' % i) if i >= 0 else '-1')
PY
)
    if [ "$OFFSET" -ge 0 ] && command -v unsquashfs >/dev/null 2>&1; then
      echo "pack-appimage.sh: --appimage-extract failed; using unsquashfs at offset $OFFSET"
      unsquashfs -q -d /tmp/squashfs-root -o "$OFFSET" /tmp/appimagetool >/dev/null 2>&1
    elif [ "$OFFSET" -ge 0 ] && ! command -v unsquashfs >/dev/null 2>&1; then
      echo "pack-appimage.sh: --appimage-extract failed and unsquashfs is not installed" >&2
    fi
  fi
fi
if [ -x "$TOOLBIN" ]; then
  TOOL="$TOOLBIN"
  echo "pack-appimage.sh: using $TOOLBIN"
elif [ "$OFFSET" = "-1" ]; then
  echo "pack-appimage.sh: downloaded appimagetool is not an AppImage (no hsqs)" >&2
  exit 1
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

# Icon: appimagetool REQUIRES an icon file matching the desktop Icon= entry
# (modern appimagetool fails the build when it is missing, check0820).  The
# docs mirror carries ahk_logo.svg (scalable) but appimagetool needs a
# raster: generate a 256x256 PNG with python3 (stdlib only) when the SVG
# cannot be converted (no rsvg/convert available).
mkdir -p "$APP/usr/share/icons/hicolor/256x256/apps"
if command -v convert >/dev/null 2>&1 && [ -f docs-v2/docs/static/ahk_logo.svg ]; then
  convert -background none -resize 256x256 docs-v2/docs/static/ahk_logo.svg \
    "$APP/usr/share/icons/hicolor/256x256/apps/autohotkey.png" 2>/dev/null \
    || true
fi
if [ ! -f "$APP/usr/share/icons/hicolor/256x256/apps/autohotkey.png" ]; then
  python3 - <<'PY'
import struct, zlib
w = h = 256
# A simple blue rounded square with "AHK" is fine; keep it minimal: solid
# dark-blue square with a lighter inner block.
row = bytearray()
pix = []
for y in range(h):
    row = bytearray()
    for x in range(w):
        if 16 <= x < 240 and 16 <= y < 240:
            row += bytes((46, 92, 158, 255))
        else:
            row += bytes((20, 40, 70, 255))
    pix.append(bytes(row))
def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(b"".join(pix))) + chunk(b"IEND", b"")
open("/tmp/ahk_icon.png", "wb").write(png)
PY
  cp /tmp/ahk_icon.png "$APP/usr/share/icons/hicolor/256x256/apps/autohotkey.png"
fi
cp "$APP/usr/share/icons/hicolor/256x256/apps/autohotkey.png" "$APP/.DirIcon" 2>/dev/null || true
# appimagetool also looks for the icon at the AppDir root (named exactly as
# the desktop Icon= value): copy it there too (check0820).
cp "$APP/usr/share/icons/hicolor/256x256/apps/autohotkey.png" "$APP/autohotkey.png" 2>/dev/null || true
cp "$APP/autohotkey.desktop" "$APP/usr/share/applications/"

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
# Do NOT swallow the tool's stderr during the build: on GitHub-hosted
# runners a failure currently produces nothing (and `> /dev/null` hid the
# cause).  Capture the run so the real error surfaces in the CI log.
ERRLOG="$OUT.run.log"
if ! "$TOOL" --no-appstream "$APP" "$OUT" >/dev/null 2>"$ERRLOG"; then
  if ! "$TOOL" "$APP" "$OUT" >/dev/null 2>>"$ERRLOG"; then
    echo "pack-appimage.sh: appimagetool failed (rc=$?)" >&2
    if [ -s "$ERRLOG" ]; then
      echo "pack-appimage.sh: appimagetool stderr follows:" >&2
      sed 's/^/  /' "$ERRLOG" | tail -20 >&2
    fi
    rm -f "$ERRLOG"
    exit 1
  fi
fi
rm -f "$ERRLOG"
chmod +x "$OUT" 2>/dev/null || true
if [ ! -s "$OUT" ]; then
  echo "pack-appimage.sh: appimagetool produced no output" >&2
  exit 1
fi
echo "built: $OUT"
ls -la "$OUT"