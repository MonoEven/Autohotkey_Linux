#!/bin/bash
# Run xgimg_diag against sway's XWayland.
set -x
D=/var/tmp/xwg
rm -rf $D; mkdir -p $D /tmp/.X11-unix
mount -o remount,rw /tmp/.X11-unix 2>/dev/null; chmod 1777 /tmp/.X11-unix 2>/dev/null
pkill -x sway; pkill -x Xwayland; sleep 1
cat > $D/config <<EOF
seat * xcursor_theme empty
input "*" xkb_layout us
output "*" bg #000000 solid_color
xwayland enable
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
echo "XWayland DISPLAY=$XD"
gcc -o /tmp/xgimg_diag /mnt/f/AI/Codex/Autohotkey_Linux/tests/doccheck/xgimg_diag.c -lX11 || exit 1
export DISPLAY=$XD
/tmp/xgimg_diag
echo "--- root window info ---"
xdpyinfo | grep -E "dimensions|depth of root|number of visuals" | head
kill $SWAYPID 2>/dev/null
