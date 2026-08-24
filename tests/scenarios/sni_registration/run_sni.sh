#!/bin/bash
# sni_registration scenario: the default tray icon must inherit AutoHotkey's
# official icon (IconName=autohotkey + a non-empty IconPixmap), and the host
# must query the registered StatusNotifierItem.  check_detail0821 §5-M5.
set -u
AHK="${AHK:?runner must export AHK}"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY
cat > /tmp/scn_sni.ahk <<'EOF'
#Requires AutoHotkey v2.0
Persistent(True)
TraySetIcon()  ; omitted FileName restores the official AutoHotkey icon
A_TrayMenu.Add("SNI-TEST-ITEM", (*) => 0)
FileAppend("ready`n", "/tmp/scn_sni_out.txt")
SetTimer(() => ExitApp(), 9000)
EOF
rm -f /tmp/scn_sni_out.txt /tmp/scn_sni_icon.txt /tmp/scn_sni_pixmap.txt
dbus-monitor "interface='org.freedesktop.DBus.Properties',type='method_call'" > /tmp/scn_sni_mon.log 2>&1 &
MON=$!
"$AHK" /tmp/scn_sni.ahk > /tmp/scn_sni_run.log 2>&1 &
AP=$!
sleep 3
BUS="org.kde.StatusNotifierItem-$AP-1"
gdbus call --session --dest "$BUS" --object-path /StatusNotifierItem \
  --method org.freedesktop.DBus.Properties.Get org.kde.StatusNotifierItem IconName \
  > /tmp/scn_sni_icon.txt 2>&1 || true
gdbus call --session --dest "$BUS" --object-path /StatusNotifierItem \
  --method org.freedesktop.DBus.Properties.Get org.kde.StatusNotifierItem IconPixmap \
  > /tmp/scn_sni_pixmap.txt 2>&1 || true
sleep 3
kill "$MON" "$AP" 2>/dev/null
# Pass only when the script ran, the default IconName is autohotkey, the
# upstream icon produced a non-empty pixmap, and the host queried the SNI.
if [ -f /tmp/scn_sni_out.txt ] \
   && grep -q 'autohotkey' /tmp/scn_sni_icon.txt \
   && grep -Eq '\([1-9][0-9]*, [1-9][0-9]*,' /tmp/scn_sni_pixmap.txt \
   && grep -Fq "destination=$BUS" /tmp/scn_sni_mon.log 2>/dev/null \
   && grep -Fq 'path=/StatusNotifierItem;' /tmp/scn_sni_mon.log 2>/dev/null; then
  touch /tmp/scn_sni_registration
fi
exit 0
