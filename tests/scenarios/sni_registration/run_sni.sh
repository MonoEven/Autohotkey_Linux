#!/bin/bash
# sni_registration scenario: TraySetIcon + A_TrayMenu must register a
# StatusNotifierItem on the session bus that a host queries (check_detail0821
# §5-M5).  The runner skips it when the needs gate fails (no GNOME session).
set -u
AHK="${AHK:?runner must export AHK}"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
ICON=""
for p in /usr/share/icons/hicolor/48x48/apps/utilities-terminal.png /usr/share/pixmaps/*.png; do
  [ -f "$p" ] && { ICON="$p"; break; }
done
cat > /tmp/scn_sni.ahk <<'EOF'
#Requires AutoHotkey v2.0
Persistent(True)
TraySetIcon("ICONPATH")
A_TrayMenu.Add("SNI-TEST-ITEM", (*) => 0)
FileAppend("ready`n", "/tmp/scn_sni_out.txt")
SetTimer(() => ExitApp(), 8000)
EOF
sed "s|ICONPATH|$ICON|" /tmp/scn_sni.ahk > /tmp/scn_sni2.ahk
rm -f /tmp/scn_sni_out.txt
dbus-monitor "interface='org.freedesktop.DBus.Properties',type='method_call'" > /tmp/scn_sni_mon.log 2>&1 &
MON=$!
"$AHK" /tmp/scn_sni2.ahk > /tmp/scn_sni_run.log 2>&1 &
AP=$!
sleep 7
kill "$MON" "$AP" 2>/dev/null
# Pass: the script ran AND a host queried org.kde.StatusNotifierItem.
if [ -f /tmp/scn_sni_out.txt ] && grep -q 'StatusNotifierItem' /tmp/scn_sni_mon.log 2>/dev/null; then
  touch /tmp/scn_sni_registration
fi
exit 0