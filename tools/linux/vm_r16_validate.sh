#!/bin/bash
# vm_r16_validate.sh -- validate R1-6 --diag + auto selection on the VM.
set -u
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
R=/tmp/xtest_r16_result.txt
exec > "$R" 2>&1
echo "r16_start=$(date -u +%T)"

echo "=== 1. --diag in GNOME session env (portal present, ext enabled) ==="
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
"$AHK" --diag 2>&1 | grep -E '^(version|session|wayland|x11|input-backend|  caps|clipboard-change|gnome-extension|gnome-major|portal-|uinput)' 
echo "--- (exit $?) ---"

echo "=== 2. auto selection with extension ENABLED (expect gnome-shell) ==="
cat > /tmp/r16_sel.ahk <<'EOF'
#Requires AutoHotkey v2.0
Persistent
Sleep 1000
ExitApp
EOF
timeout 15 "$AHK" /tmp/r16_sel.ahk 2>&1 | head -3
echo "backend_signal=$(timeout 15 "$AHK" --diag 2>&1 | grep '^input-backend')"

echo "=== 3. auto selection with extension DISABLED (expect portal, GNOME49 backend present, NO error) ==="
gnome-extensions disable ahk-global-hotkeys@autohotkey.org >/dev/null 2>&1
sleep 1
timeout 15 "$AHK" --diag 2>&1 | grep -E '^(input-backend|gnome-extension|gnome-major|portal-)'
echo "--- run a script with ext disabled, capture stderr (should have NO AHK warning) ---"
timeout 15 "$AHK" /tmp/r16_sel.ahk 2>&1 | grep -i 'AHK warning' || echo "no_warning_ok"
gnome-extensions enable ahk-global-hotkeys@autohotkey.org >/dev/null 2>&1

echo "=== 4. headless (no bus, no display): probe must fail closed ==="
env -u DBUS_SESSION_BUS_ADDRESS -u WAYLAND_DISPLAY -u DISPLAY -u XDG_CURRENT_DESKTOP -u XDG_SESSION_TYPE -u XAUTHORITY XDG_RUNTIME_DIR=/tmp/nonexistent "$AHK" --diag 2>&1 | grep -E '^(session|input-backend|gnome-major|portal-)'

echo "r16_done=1"
