#!/bin/bash
# Diagnose combo keys + buttons on sway virtual devices (logs in /var/tmp).
cd /mnt/f/AI/Codex/Autohotkey_Linux/tests/doccheck || exit 1
T=/var/tmp/wldiag
mkdir -p $T
pkill -x sway 2>/dev/null
pkill -x Xwayland 2>/dev/null
sleep 0.5
rm -rf $T/swayhome
mkdir -p $T/swayhome
rm -f $T/wl_key_sr $T/wl_btn3 $T/wl_key_shift_l
cat > $T/swayhome/config <<'EOF'
seat * xcursor_theme empty
input "*" xkb_layout us
output "*" bg #000000 solid_color
xwayland disable
bindsym Shift+Return exec touch /var/tmp/wldiag/wl_key_sr
bindsym button3 exec touch /var/tmp/wldiag/wl_btn3
bindsym Shift_L exec touch /var/tmp/wldiag/wl_key_shift_l
EOF
cd $T/swayhome
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 XDG_RUNTIME_DIR=$T/swayhome \
  sway -d -c $T/swayhome/config > $T/sway.log 2>&1 &
SWAYPID=$!
sleep 3
cat > $T/wl_diag2.ahk <<'EOF'
#Requires AutoHotkey v2.0
Send("+{Return}")
Sleep(800)
; Mouse buttons: sway's bindsym buttonN requires the pointer over a
; surface, so create a window and move the pointer onto it first.
h := ToolTip("BtnTip")
Sleep(1200)
MouseMove(640, 360)
Sleep(600)
MouseClick("Right")
Sleep(800)
ToolTip()
ExitApp(0)
EOF
XDG_RUNTIME_DIR=$T/swayhome WAYLAND_DISPLAY=wayland-1 DISPLAY= \
  timeout 20 /mnt/f/AI/Codex/Autohotkey_Linux/build-core/source/linux/core/ahk_core $T/wl_diag2.ahk > $T/out.txt 2>&1
echo "AHK_RC=$?"
echo "key_sr: $(ls $T/wl_key_sr 2>/dev/null || echo MISSING)"
echo "shift_l: $(ls $T/wl_key_shift_l 2>/dev/null || echo MISSING)"
echo "btn3: $(ls $T/wl_btn3 2>/dev/null || echo MISSING)"
echo "--- sway log: all key/seat/binding lines ---"
grep -inE 'handle_keyboard_key|running command|binding|keysym|notify_key|seat_execute|modifiers' $T/sway.log | tail -25
kill $SWAYPID 2>/dev/null
