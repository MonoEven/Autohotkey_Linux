#!/bin/bash
# vm_r16_errorpath.sh -- verify the auto-selection ERROR path (GNOME without
# extension AND without a GlobalShortcuts portal backend => loud warning).
set -u
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
R=/tmp/xtest_r16_errorpath.txt
exec > "$R" 2>&1
cat > /tmp/r16_sel2.ahk <<'EOF'
#Requires AutoHotkey v2.0
Persistent
Sleep 800
ExitApp
EOF

echo "=== simulate GNOME Wayland with a DEAD session bus (no ext, no portal) ==="
echo "--- first confirm extension bus name is unreachable with a dead bus ---"
env -u WAYLAND_DISPLAY -u DISPLAY -u XAUTHORITY \
  XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=ubuntu:GNOME \
  DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/nonexistent_bus \
  XDG_RUNTIME_DIR=/tmp/nonexistent_rt \
  "$AHK" /tmp/r16_sel2.ahk 2>&1 | head -8

echo "=== same but with the REAL session env and extension disabled (portal present => no warning) ==="
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export WAYLAND_DISPLAY=wayland-0
export DISPLAY=:0
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=ubuntu:GNOME
if [ -z "${XAUTHORITY:-}" ]; then
  XAUTH=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
  [ -n "$XAUTH" ] && export XAUTHORITY="$XAUTH"
fi
gnome-extensions disable ahk-global-hotkeys@autohotkey.org >/dev/null 2>&1
sleep 1
echo "--- stderr of a run (expect NO AHK warning) ---"
timeout 15 "$AHK" /tmp/r16_sel2.ahk 2>&1 | grep -i 'AHK warning' || echo "no_warning_ok"
gnome-extensions enable ahk-global-hotkeys@autohotkey.org >/dev/null 2>&1
echo "r16_errorpath_done=1"
