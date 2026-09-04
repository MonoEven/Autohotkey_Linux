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

VER="${1:-2.0.26-linux.20}"
ARCH=$(uname -m)
case "$ARCH" in
  x86_64) ARCH=x86_64 ;;
  aarch64|arm64) ARCH=aarch64 ;;
esac

CORE=${AHK_CORE:-build-core/source/linux/core/ahk_core}
INPUTD=${AHK_INPUTD:-build-core/source/linux/inputd/ahk-inputd}
PACK_CORE=${AHK_PACK_CORE:-build-pack-runtime/source/linux/core/ahk_core}
if [ ! -x "$CORE" ]; then
  echo "pack-appimage.sh: $CORE not found; build first" >&2
  exit 1
fi
if [ ! -x "$INPUTD" ]; then
  echo "pack-appimage.sh: $INPUTD not found; build first" >&2
  exit 1
fi
if ldd "$CORE" 2>/dev/null | grep -q 'libei\.so\.1'; then
  [ -x "$PACK_CORE" ] || { echo "pack-appimage.sh: missing feature-off $PACK_CORE" >&2; exit 1; }
else
  PACK_CORE="$CORE"
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
# appimagetool's own AppRun adds its BUNDLED mksquashfs to PATH - using
# it (instead of the inner ELF directly) is what makes the tool work on
# a plain runner where the system squashfs-tools may differ (check0820).
TOOLBIN=/tmp/squashfs-root/AppRun
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
if [ ! -x "$TOOLBIN" ] && [ -x /tmp/squashfs-root/usr/bin/appimagetool ]; then
  TOOLBIN=/tmp/squashfs-root/usr/bin/appimagetool
fi
if [ -x "$TOOLBIN" ]; then
  TOOL="$TOOLBIN"
  echo "pack-appimage.sh: using $TOOLBIN"
elif [ "$OFFSET" = "-1" ]; then
  echo "pack-appimage.sh: downloaded appimagetool is not an AppImage (no hsqs)" >&2
  exit 1
fi
# In case the extracted AppRun itself re-executes an AppImage (older
# appimagetool releases), force the no-FUSE path explicitly.
export APPIMAGE_EXTRACT_AND_RUN=1

APP=dist/appimage/autohotkey.AppDir
rm -rf dist/appimage
mkdir -p "$APP/usr/bin" "$APP/usr/lib" "$APP/usr/share/applications" \
         "$APP/usr/share/autohotkey" \
         "$APP/usr/share/icons/hicolor/scalable/apps" \
         "$APP/usr/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org"

install -m 0755 "$CORE" "$APP/usr/bin/ahk_core"
install -m 0755 "$PACK_CORE" "$APP/usr/bin/ahk_core_pack"
install -m 0755 "$INPUTD" "$APP/usr/bin/ahk-inputd"
# A build with M6 libei support links the optional libei/liboeffis SONAMEs.
# Bundle exactly those two so the AppImage retains its consented injection
# route on hosts that do not preinstall them; other desktop ABI libraries are
# intentionally supplied by the host as before.
for soname in libei.so.1 liboeffis.so.1; do
  lib=$(ldd "$CORE" 2>/dev/null | awk -v n="$soname" '$1 == n {print $3; exit}')
  [ -n "$lib" ] && [ -f "$lib" ] && cp -L "$lib" "$APP/usr/lib/$soname"
done
if ldd "$CORE" 2>/dev/null | grep -q 'libei\.so\.1'; then
  [ -f "$APP/usr/lib/libei.so.1" ] && [ -f "$APP/usr/lib/liboeffis.so.1" ] \
    || { echo "pack-appimage.sh: failed to stage optional libei runtime" >&2; exit 1; }
fi
install -m 0644 source/resources/icon_main.ico "$APP/usr/share/autohotkey/icon_main.ico"
install -m 0644 docs-v2/docs/static/ahk16.png "$APP/usr/share/autohotkey/autohotkey.png"
# The GNOME Shell global-hotkey extension ships inside the AppImage (the
# AppDir is a read-only single file, so `AppRun --install-extension`
# copies it into the user's ~/.local/share/gnome-shell/extensions and
# enables it -- see the AppRun below).
if [ -d "$REPO_DIR/extension/ahk-global-hotkeys@autohotkey.org" ]; then
  cp -r "$REPO_DIR/extension/ahk-global-hotkeys@autohotkey.org/." \
        "$APP/usr/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org/"
fi
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
Comment=AutoHotkey v2 automation for Linux (X11/Wayland)
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
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# `ahk.AppImage --install-extension` installs the bundled GNOME Shell
# global-hotkey extension for the current user (the AppImage itself is a
# read-only single file, so the extension must be copied out of it).
EXT_UUID="ahk-global-hotkeys@autohotkey.org"
if [ "${1:-}" = "--install-extension" ]; then
  SRC="$HERE/usr/share/gnome-shell/extensions/$EXT_UUID"
  if [ ! -d "$SRC" ]; then
    echo "ahk: no extension bundled in this AppImage (build without one?)" >&2
    exit 1
  fi
  DEST="$HOME/.local/share/gnome-shell/extensions"
  mkdir -p "$DEST"
  if [ -d "$DEST/$EXT_UUID" ]; then
    echo "GNOME extension already installed ($DEST/$EXT_UUID); kept as-is."
  else
    cp -r "$SRC" "$DEST/"
    echo "GNOME extension installed: $DEST/$EXT_UUID"
  fi
  if command -v gnome-extensions >/dev/null 2>&1; then
    gnome-extensions enable "$EXT_UUID" >/dev/null 2>&1 \
      && echo "GNOME extension enabled." \
      || echo "Enable it later with: gnome-extensions enable $EXT_UUID"
  else
    echo "gnome-extensions not available; enable it from the GNOME Extensions app."
  fi
  if pgrep -x gnome-shell >/dev/null 2>&1; then
    echo "GNOME Shell is running now. Restart it so the new extension loads:"
    echo "    log out and back in, or press Alt+F2 and type 'r'"
  fi
  exit 0
fi
# `ahk.AppImage --update` downloads the newest release AppImage into the
# current directory and tells the user to replace this file with it.  (An
# AppImage cannot reliably replace its own on-disk file while running -- at
# runtime the path is the extraction mount, not the .AppImage file -- so
# self-replacement is deliberately not attempted.)
if [ "${1:-}" = "--update" ]; then
  echo "ahk --update: checking https://github.com/MonoEven/Autohotkey_Linux/releases"
  tag=$(curl -fsSL --connect-timeout 15 \
    "https://api.github.com/repos/MonoEven/Autohotkey_Linux/releases/latest" 2>/dev/null |
    printf '%s\n' | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\([^"]*\)".*/\1/p' | head -1)
  if [ -z "$tag" ]; then
    echo "ahk: could not determine the latest release (network?)" >&2
    exit 1
  fi
  arch=$(uname -m)
  case "$arch" in x86_64|amd64) arch=x86_64 ;; aarch64|arm64) arch=aarch64 ;; esac
  url="https://github.com/MonoEven/Autohotkey_Linux/releases/download/v$tag/autohotkey-linux-$tag-$arch.AppImage"
  out="autohotkey-linux-$tag-$arch.AppImage"
  echo "Downloading AutoHotkey v$tag ..."
  curl -fsSL --connect-timeout 20 -o "$out" "$url" || { echo "ahk: download failed" >&2; exit 1; }
  echo "AutoHotkey v$tag downloaded to: $PWD/$out"
  echo "Replace this AppImage file with it to update (the running file itself"
  echo "cannot be overwritten from inside the App):"
  echo "    mv $PWD/$out $SELF"
  exit 0
fi
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
# Do NOT swallow the tool's output during the build: on GitHub-hosted
# runners a failure currently produces nothing visible (and `> /dev/null`
# hid the cause).  Capture BOTH stdout and stderr so the real error shows
# up in the CI log.  appimagetool on the runner fails after printing only
# its version banner to stderr; whatever diagnostic it emits has so far
# gone to stdout (check0820 round), so neither stream is discarded.
ERRLOG="$OUT.run.log"
run_tool() { # args... ; runs with full capture, returns tool exit code
  "$TOOL" "$@" >"$ERRLOG" 2>&1
}
rc1=0; rc2=0
if ! run_tool --no-appstream "$APP" "$OUT"; then
  rc1=$?
  if ! run_tool "$APP" "$OUT"; then
    rc2=$?
    echo "pack-appimage.sh: appimagetool failed (--no-appstream rc=$rc1, plain rc=$rc2)" >&2
    if [ -s "$ERRLOG" ]; then
      echo "pack-appimage.sh: appimagetool output follows:" >&2
      sed 's/^/  /' "$ERRLOG" >&2
    fi
    # Verbose retry with the tool diagnosed on stderr: on the CI runner
    # the tool exits 0 without producing a file; show what it was doing.
    run_tool -v --no-appstream "$APP" "$OUT" || true
    if [ -s "$ERRLOG" ]; then
      echo "pack-appimage.sh: verbose retry output:" >&2
      sed 's/^/  /' "$ERRLOG" >&2
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
# Build-time sanity: the AppDir must carry inputd, the GNOME extension and the
# AppRun commands (guards a future edit that forgets them).
if [ ! -x "$APP/usr/bin/ahk-inputd" ]; then
  echo "pack-appimage.sh: AppDir missing ahk-inputd" >&2
  exit 1
fi
if [ ! -f "$APP/usr/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org/metadata.json" ]; then
  echo "pack-appimage.sh: AppDir missing the GNOME extension" >&2
  exit 1
fi
if ! grep -q -- '--install-extension' "$APP/AppRun" || ! grep -q -- '--update' "$APP/AppRun"; then
  echo "pack-appimage.sh: AppRun missing --install-extension/--update" >&2
  exit 1
fi
echo "built: $OUT"
ls -la "$OUT"