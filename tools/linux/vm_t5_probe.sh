#!/bin/bash
# vm_t5_probe.sh -- probe IBus / flatpak / pacman / makepkg on the VM.
set -u
R=/tmp/xtest_t5_probe.txt
exec > "$R" 2>&1
echo "=== IBus ==="
for c in ibus ibus-daemon; do command -v "$c" >/dev/null 2>&1 && echo "$c: YES" || echo "$c: no"; done
pgrep -f ibus-daemon | head -2 || echo "(no ibus-daemon)"
echo "=== IBus engine support files ==="
ls /usr/share/ibus/component/*.xml 2>/dev/null | head -4 || echo "(none)"
echo "=== flatpak ==="
command -v flatpak >/dev/null 2>&1 && echo "flatpak: YES" || echo "flatpak: no"
command -v flatpak-builder >/dev/null 2>&1 && echo "flatpak-builder: YES" || echo "flatpak-builder: no"
echo "=== AUR/pacman ==="
command -v pacman >/dev/null 2>&1 && echo "pacman: YES" || echo "pacman: no"
command -v makepkg >/dev/null 2>&1 && echo "makepkg: YES" || echo "makepkg: no"
command -v fakeroot >/dev/null 2>&1 && echo "fakeroot: YES" || echo "fakeroot: no"
echo "=== existing distro files ==="
ls /home/mono/Autohotkey_Linux/dist/ 2>/dev/null | head -8 || echo "(no dist dir)"
ls /home/mono/Autohotkey_Linux/packaging/ 2>/dev/null | head -8 || echo "(no packaging dir)"
echo "t5_probe_done=1"