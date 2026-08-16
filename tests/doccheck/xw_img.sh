#!/bin/bash
# Debug: ImageSearch XGetImage hang on sway's XWayland.
# Runs sway WITH xwayland, then assert_image.ahk against DISPLAY=<sway's>.
set -x
D=/var/tmp/xwimg
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
# Minimal repro: XGetImage of the root window region via the port's ImageSearch.
mkdir -p /tmp/ahk_dc_img
cat > /tmp/ahk_dc_img/red2.ppm <<'EOF'
P3
2 2
255
255 0 0 255 0 0
255 0 0 255 0 0
EOF
cat > $D/img_min.ahk <<'EOF'
#Requires AutoHotkey v2.0
; draw a red box on the root so XGetImage has something? XWayland root is
; managed by sway; XGetImage of the root window may not be readable.
p := "0-0 10 20 40 30"  ; not used
try {
    r := ImageSearch(&px, &py, 0, 0, 200, 150, "*10 /tmp/ahk_dc_img/red2.ppm")
    FileAppend("ImageSearch rc=" r " x=" px " y=" py "`n", "/tmp/xw_img.txt")
} catch as e {
    FileAppend("ImageSearch err: " Type(e) " " e.Message "`n", "/tmp/xw_img.txt")
}
ExitApp(0)
EOF
rm -f /tmp/xw_img.txt
timeout 20 /mnt/f/AI/Codex/Autohotkey_Linux/build-core/source/linux/core/ahk_core $D/img_min.ahk > $D/run.log 2>&1
echo "runner rc=$? (124 = hang)"
cat /tmp/xw_img.txt 2>/dev/null || echo "no img txt"
tail -5 $D/run.log
kill $SWAYPID 2>/dev/null
