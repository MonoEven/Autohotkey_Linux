#!/bin/bash
# Debug v4: screencopy capture WITHOUT any X window - what do we get?
# sway bg is #000000; XWayland root would be white if we capture the
# XWayland surface instead of the output.
set -x
D=/var/tmp/xwimg4
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
export DISPLAY="$XD" XDG_RUNTIME_DIR=$D
unset WAYLAND_DISPLAY
cat > $D/pix.ahk <<'EOF'
#Requires AutoHotkey v2.0
for pt in [[0,0],[640,360],[100,100]] {
    try {
        c := PixelGetColor(pt[1], pt[2])
        FileAppend("(" pt[1] "," pt[2] ")=" c "`n", "/tmp/xw_img4.txt")
    } catch as e {
        FileAppend("(" pt[1] "," pt[2] ") err: " Type(e) "`n", "/tmp/xw_img4.txt")
    }
}
ExitApp(0)
EOF
rm -f /tmp/xw_img4.txt
timeout 30 /mnt/f/AI/Codex/Autohotkey_Linux/build-core/source/linux/core/ahk_core $D/pix.ahk > $D/run.log 2>&1
echo "runner rc=$?"
cat /tmp/xw_img4.txt 2>/dev/null || echo "no txt"
kill $SWAYPID 2>/dev/null
