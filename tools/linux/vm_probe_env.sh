#!/bin/bash
# vm_probe_env.sh -- environment fact-gathering for R1-6 (GNOME 48+ portal)
# and R1-7 (XWayland XTEST->libei) verification on the GNOME 49 VM.
# Read-only; prints one line per fact.  Run as the session user.
set -u
echo "uid=$(id -u) user=$(id -un)"
echo "session=${XDG_SESSION_TYPE:-<unset>}"
echo "desktop=${XDG_CURRENT_DESKTOP:-<unset>}"
echo "wayland=${WAYLAND_DISPLAY:-<unset>}"
echo "display=${DISPLAY:-<unset>}"
echo "xdg_runtime=${XDG_RUNTIME_DIR:-<unset>}"
echo "dbus_addr=${DBUS_SESSION_BUS_ADDRESS:-<unset>}"

echo "--- versions ---"
echo "gnome_shell=$(gnome-shell --version 2>/dev/null || echo '<none>')"
echo "xdg_desktop_portal_gnome=$(dpkg-query -W -f='${Version}' xdg-desktop-portal-gnome 2>/dev/null || echo '<not installed>')"
echo "xdg_desktop_portal=$(dpkg-query -W -f='${Version}' xdg-desktop-portal 2>/dev/null || echo '<not installed>')"
echo "xwayland=$(Xwayland -version 2>&1 | head -1 || echo '<none>')"
echo "libei=$(dpkg-query -W -f='${Version}' libei1 2>/dev/null || echo '<not installed>')"
echo "liboeffis=$(dpkg-query -W -f='${Version}' liboeffis1 2>/dev/null || echo '<not installed>')"

echo "--- gnome shell version file ---"
cat /usr/share/gnome-shell/gnome-shell-version 2>/dev/null || echo "<no version file>"

echo "--- portal name on bus ---"
busctl --user list 2>/dev/null | grep -i portal || echo "<no portal bus names (grep) -> see full list>"
busctl --user list 2>/dev/null | grep -iE "portal|gnome" || true

echo "--- GlobalShortcuts interface version property (functional probe) ---"
gdbus call --session --dest org.freedesktop.portal.Desktop \
  --object-path /org/freedesktop/portal/desktop \
  --method org.freedesktop.DBus.Properties.Get \
  org.freedesktop.portal.GlobalShortcuts version 2>&1 || echo "GS version probe failed"

echo "--- GlobalShortcuts methods present? ---"
gdbus introspect --session --dest org.freedesktop.portal.Desktop \
  --object-path /org/freedesktop/portal/desktop 2>/dev/null \
  | grep -A30 "interface org.freedesktop.portal.GlobalShortcuts" | head -40

echo "--- extension installed / enabled ---"
ls -d "$HOME/.local/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org" 2>/dev/null \
  && echo "ext_dir=user" || echo "ext_dir_user=<absent>"
ls -d /usr/share/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org 2>/dev/null \
  && echo "ext_dir=system" || echo "ext_dir_system=<absent>"
echo "ext_list=$(gnome-extensions list 2>/dev/null | tr '\n' ' ')"
echo "ext_enabled=$(gnome-extensions info ahk-global-hotkeys@autohotkey.org 2>/dev/null | grep -iE 'State|Enabled' | tr '\n' ' ')"

echo "--- app-id desktop file (portal app-id resolvability) ---"
ls -la "$HOME/.local/share/applications/org.autohotkey.linux.desktop" 2>/dev/null || echo "app_desktop_user=<absent>"
ls -la /usr/share/applications/org.autohotkey.linux.desktop 2>/dev/null || echo "app_desktop_system=<absent>"

echo "--- XWayland running? (ps) ---"
ps -eo pid,args | grep -i "[X]wayland" || echo "<no Xwayland process>"

echo "--- a11y / input capture (context) ---"
gsettings get org.gnome.desktop.interface toolkit-accessibility 2>/dev/null || true
gdbus introspect --session --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop 2>/dev/null | grep -iE "InputCapture|RemoteDesktop" | head -10 || true

echo "--- GNOME lock / session env ---"
echo "loginctl_session=$(loginctl show-session $(loginctl list-sessions --no-legend 2>/dev/null | awk '{print $1}' | head -1) -p Type -p Active 2>/dev/null | tr '\n' ' ')"
echo VM_PROBE_DONE
