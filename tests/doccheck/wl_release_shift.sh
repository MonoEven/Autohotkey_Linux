#!/bin/bash
# Verify: does bindsym --release Shift_L fire when virtual keyboard sends
# Shift_L down/up WITH explicit modifiers push (real-keyboard semantics)?
set -x
D=/var/tmp/wlrel
rm -rf $D; mkdir -p $D
pkill -x sway; sleep 1
rm -f /tmp/t_rel_shift /tmp/t_plain_shift
cat > $D/config <<EOF
seat * xcursor_theme empty
input "*" xkb_layout us
output "*" bg #000000 solid_color
xwayland disable
bindsym Shift_L exec touch /tmp/t_plain_shift
bindsym --release Shift_L exec touch /tmp/t_rel_shift
EOF
cd $D || exit 1
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 XDG_RUNTIME_DIR=$D sway -c $D/config > $D/sway.log 2>&1 &
SWAYPID=$!
sleep 3
export XDG_RUNTIME_DIR=$D WAYLAND_DISPLAY=wayland-1
unset DISPLAY
timeout 20 /mnt/f/AI/Codex/Autohotkey_Linux/build-core/source/linux/core/ahk_core /dev/stdin <<'EOF' > /dev/null 2>&1
#Requires AutoHotkey v2.0
Send("{Shift down}{Shift up}")
Sleep(500)
ExitApp(0)
EOF
sleep 1
echo "plain Shift_L binding: $(ls /tmp/t_plain_shift 2>/dev/null || echo NO)"
echo "release Shift_L binding: $(ls /tmp/t_rel_shift 2>/dev/null || echo NO)"
kill $SWAYPID 2>/dev/null
grep -E "running command|binding" $D/sway.log | tail
