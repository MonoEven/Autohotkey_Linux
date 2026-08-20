#!/bin/bash
# Apply the AutoHotkey evdev/uinput permission artifacts (check0820).
#
# 1. udev rule for a stable, world-writable /dev/uinput (needed by the
#    virtual-keyboard and uinput-replay lanes).
# 2. input-group membership for reading /dev/input/event* (the capture
#    lane; the kernel default is root:input 0660).
# 3. (optional) polkit action for a future root capture helper - policy
#    file is installed but unused today.
#
# Usage: sudo bash install_permissions.sh <user>
set -eu

USER_ARG="${1:-$USER}"
[ "$(id -u)" -eq 0 ] || { echo "run as root (sudo)"; exit 1; }

echo "== udev rule: /dev/uinput world-writable =="
install -m 0644 60-ahk-uinput.rules /etc/udev/rules.d/
udevadm control --reload-rules
udevadm trigger --subsystem-match=input || true
ls -la /dev/uinput

echo "== input-group membership for $USER_ARG =="
usermod -aG input "$USER_ARG" || echo "WARN: usermod failed (user $USER_ARG?)"
id "$USER_ARG" | grep -o 'groups=[^ ]*' || true

echo "== polkit action (future inputd helper; inert today) =="
install -m 0644 io.github.autohotkey.inputd.policy /usr/share/polkit-1/actions/
ls -la /usr/share/polkit-1/actions/io.github.autohotkey.inputd.policy

cat <<'EOF'
Done.  Notes:
  - the input-group change needs a re-login to take effect;
  - today's port needs no root daemon: capture = input-group read,
    replay/virtual-keyboard = /dev/uinput (0666).  The polkit action is
    reserved for a later privileged capture helper (EVIOCGRAB suppress).
EOF