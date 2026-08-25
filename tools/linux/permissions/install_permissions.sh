#!/bin/bash
# Apply the AutoHotkey evdev/uinput permission artifacts (check0820).
#
# 1. udev rule for a stable, world-writable /dev/uinput (needed by the
#    virtual-keyboard and uinput-replay lanes).
# 2. input-group membership for reading /dev/input/event* (the capture
#    lane; the kernel default is root:input 0660).
# 3. The packaged root ahk-inputd service is authorized by its root:input 0660
#    socket; the historical polkit action remains reserved and is not used by
#    the current socket-activation path.
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

echo "== reserved polkit action (not used by socket-activated inputd) =="
install -m 0644 io.github.autohotkey.inputd.policy /usr/share/polkit-1/actions/
ls -la /usr/share/polkit-1/actions/io.github.autohotkey.inputd.policy

cat <<'EOF'
Done.  Notes:
  - the input-group change needs a re-login to take effect;
  - in-process capture uses input-group read and /dev/uinput; packaged
    ahk-inputd instead runs as a root systemd service behind root:input 0660;
  - enable it explicitly: systemctl enable --now ahk-inputd.socket;
  - the polkit action is reserved and is not consulted by this service.
EOF