#!/bin/bash
# Determine sway's REAL behavior for bindsym Shift_L (modifier key alone):
# send a physical-path key event via XWayland/XTEST and see if the
# modifier-only binding fires.  Then compare with our virtual keyboard.
set -x
D=/var/tmp/wlsx
rm -rf $D; mkdir -p $D /tmp/.X11-unix
mount -o remount,rw /tmp/.X11-unix 2>/dev/null; chmod 1777 /tmp/.X11-unix 2>/dev/null
pkill -x sway; pkill -x Xwayland; sleep 1
rm -f /tmp/t_shift_real /tmp/t_sr_real
cat > $D/config <<EOF
seat * xcursor_theme empty
input "*" xkb_layout us
output "*" bg #000000 solid_color
xwayland enable
bindsym Shift_L exec touch /tmp/t_shift_real
bindsym Shift+Return exec touch /tmp/t_sr_real
EOF
cd $D || exit 1
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 XDG_RUNTIME_DIR=$D sway -c $D/config > $D/sway.log 2>&1 &
SWAYPID=$!
sleep 3
for d in 0 1 2 3 4; do
  if [ -S "/tmp/.X11-unix/X$d" ] && DISPLAY=:$d timeout 5 xdpyinfo >/dev/null 2>&1; then
    if [ "$(stat -c %u "/tmp/.X11-unix/X$d" 2>/dev/null)" = "$(id -u)" ]; then
      XD=:$d; break
    fi
  fi
done
echo "XWayland display: $XD"
# 1) modifier key alone via real path
DISPLAY=$XD xdotool keydown Shift_L
sleep 0.5
DISPLAY=$XD xdotool keyup Shift_L
sleep 0.5
echo "after Shift_L: $(ls /tmp/t_shift_real 2>/dev/null || echo MISSING)"
# 2) modifier combo via real path
DISPLAY=$XD xdotool keydown Shift_L
sleep 0.3
DISPLAY=$XD xdotool key Return
sleep 0.3
DISPLAY=$XD xdotool keyup Shift_L
sleep 0.5
echo "after Shift+Return: $(ls /tmp/t_sr_real 2>/dev/null || echo MISSING)"
kill $SWAYPID 2>/dev/null
grep -E "running command|binding" $D/sway.log | tail -20
