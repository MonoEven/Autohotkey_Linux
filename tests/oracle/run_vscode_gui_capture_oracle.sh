#!/bin/bash
# GUI-1 VS Code/Electron matrix. Proves X11 window enumeration and Wayland
# AT-SPI window/document discovery on real VS Code. It also locks the current
# honest limitation: VS Code 1.134.0 exposes only U+FFFC for Document.Text and
# no Monaco content node even with forced accessibility + accessibilitySupport.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
command -v code >/dev/null || { echo VSCODE_GUI_SKIP code-not-installed; exit 2; }
version="$(code --version | head -1)"

# X11/EWMH window capture under Xvfb.
XWORK=/tmp/vscode-x11-work
XUSER=/tmp/vscode-x11-user
XEXT=/tmp/vscode-x11-ext
rm -rf "$XWORK" "$XUSER" "$XEXT" /tmp/vscode_x11.out /tmp/vscode_x11.log
mkdir -p "$XWORK" "$XUSER" "$XEXT"
printf 'X11-CAPTURE\n' >"$XWORK/VSCODE-X11-CAPTURE.txt"
cat >/tmp/vscode_x11.ahk <<'EOF'
#Requires AutoHotkey v2.0
count := WinGetList("VSCODE-X11-CAPTURE.txt").Length
FileAppend("count=" count "`n", "/tmp/vscode_x11.out")
ExitApp
EOF
xvfb-run -a bash -c "code --user-data-dir '$XUSER' --extensions-dir '$XEXT' \
  --disable-extensions --disable-gpu --no-sandbox --disable-workspace-trust \
  --skip-welcome --ozone-platform=x11 --wait --new-window \
  '$XWORK/VSCODE-X11-CAPTURE.txt' >/tmp/vscode_x11.log 2>&1 & CPID=\$!; \
  sleep 10; '$BIN' /tmp/vscode_x11.ahk >/tmp/vscode_x11_ahk.log 2>&1; \
  kill \$CPID 2>/dev/null; wait \$CPID 2>/dev/null"
x11_count="$(sed -n 's/^count=//p' /tmp/vscode_x11.out | head -1)"
[ -n "$x11_count" ] && [ "$x11_count" -ge 1 ] \
  || { echo VSCODE_X11_CAPTURE_FAIL; cat /tmp/vscode_x11.out /tmp/vscode_x11.log 2>/dev/null; exit 1; }
pkill -f "$XUSER" 2>/dev/null

# Wayland/AT-SPI window and current editor-content limitation.
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
WUSER=/tmp/vscode-wayland-user
WEXT=/tmp/vscode-wayland-ext
WWORK=/tmp/vscode-wayland-work
FILE="$WWORK/VSCODE-WAYLAND-CAPTURE.txt"
TITLE="VSCODE-WAYLAND-CAPTURE.txt - vscode-wayland-work - Visual Studio Code"
rm -rf "$WUSER" "$WEXT" "$WWORK" /tmp/vscode_wayland.out \
  /tmp/vscode_wayland.dump /tmp/vscode_wayland.log
mkdir -p "$WUSER/User" "$WEXT" "$WWORK"
cat >"$WUSER/User/settings.json" <<'EOF'
{
  "editor.accessibilitySupport": "on",
  "workbench.startupEditor": "none",
  "security.workspace.trust.enabled": false,
  "telemetry.telemetryLevel": "off"
}
EOF
printf 'VSCODE-CAPTURE-Ω\nsecond line\n' >"$FILE"
code --user-data-dir "$WUSER" --extensions-dir "$WEXT" --disable-extensions \
  --disable-gpu --no-sandbox --disable-workspace-trust --skip-welcome \
  --force-renderer-accessibility --ozone-platform=wayland \
  --enable-features=UseOzonePlatform --wait --new-window "$WWORK" "$FILE" \
  >/tmp/vscode_wayland.log 2>&1 &
CPID=$!
sleep 14
cat >/tmp/vscode_wayland.ahk <<'EOF'
#Requires AutoHotkey v2.0
title := "VSCODE-WAYLAND-CAPTURE.txt - vscode-wayland-work - Visual Studio Code"
text := ControlGetText(title, title)
cp := StrLen(text) ? Ord(SubStr(text, 1, 1)) : 0
FileAppend("len=" StrLen(text) " cp=" cp "`n", "/tmp/vscode_wayland.out")
ExitApp
EOF
AHK_ATSPI_DUMP=/tmp/vscode_wayland.dump timeout -k 2 30 "$BIN" /tmp/vscode_wayland.ahk \
  >/tmp/vscode_wayland_ahk.log 2>&1
rc=$?
kill "$CPID" 2>/dev/null; wait "$CPID" 2>/dev/null
pkill -f "$WUSER" 2>/dev/null
[ "$rc" = 0 ] || { echo VSCODE_WAYLAND_AHK_FAIL; cat /tmp/vscode_wayland_ahk.log; exit 1; }
title_line="$(grep -F "$TITLE" /tmp/vscode_wayland.dump | head -1)"
printf '%s' "$title_line" | grep -q 'interfaces=.*org.a11y.atspi.Text.*org.a11y.atspi.Document' \
  || { echo VSCODE_ATSPI_WINDOW_FAIL; printf '%s\n' "$title_line"; exit 1; }
dest="$(printf '%s' "$title_line" | sed -n 's/.*dest=\([^[:space:]]*\).*/\1/p')"
[ -n "$dest" ] || { echo VSCODE_ATSPI_DEST_FAIL; exit 1; }
node_count="$(grep -c "dest=$dest" /tmp/vscode_wayland.dump)"
# Versioned limitation oracle: document has only embedded-object placeholder
# and the unique source content is absent from every cached accessible name.
grep -q '^len=1 cp=65532$' /tmp/vscode_wayland.out \
  || { echo VSCODE_TEXT_LIMIT_CHANGED; cat /tmp/vscode_wayland.out; exit 1; }
if grep "dest=$dest" /tmp/vscode_wayland.dump | grep -q 'VSCODE-CAPTURE-Ω'; then
  echo VSCODE_CONTENT_NOW_EXPOSED_UPDATE_MATRIX
  exit 1
fi
cat >"$OUT/vscode-gui-capture-summary.json" <<EOF
{"schema":1,"result":"pass","vscode_version":"$version","x11_window_count":$x11_count,"wayland_window":true,"wayland_destination":"$dest","wayland_nodes":$node_count,"document_interfaces":true,"document_text_codepoint":65532,"monaco_content_exposed":false,"launch_flags":["--force-renderer-accessibility","editor.accessibilitySupport=on"]}
EOF
echo "VSCODE_GUI_CAPTURE_ORACLE_PASS version=$version x11=$x11_count wayland_window=1 monaco_content=unavailable"
