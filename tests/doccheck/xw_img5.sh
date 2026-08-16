#!/bin/bash
# Debug v5: locate the full red rect with PixelSearch on XWayland.
set -x
D=/var/tmp/xwimg5
rm -rf $D; mkdir -p $D /tmp/.X11-unix
mount -o remount,rw /tmp/.X11-unix 2>/dev/null; chmod 1777 /tmp/.X11-unix 2>/dev/null
pkill -x sway; pkill -x Xwayland; sleep 1
cat > $D/config <<EOF
seat * xcursor_theme empty
input "*" xkb_layout us
output "*" bg #000000 solid_color
xwayland enable
for_window [class="DocCheck"] floating enable
for_window [class="DocCheck"] border none
for_window [class="DocCheck"] resize set 300 200
for_window [class="DocCheck"] move position 50 60
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
/mnt/f/AI/Codex/Autohotkey_Linux/tests/doccheck/out/xwin_helper -title ImgMain -class DocCheck -x 50 -y 60 -w 300 -h 200 -fill FF0000 10 20 40 30 &
sleep 2
cat > $D/find.ahk <<'EOF'
#Requires AutoHotkey v2.0
CoordMode("Pixel", "Screen")
found := 0
r := PixelSearch(&found, &fy, 0, 0, 1279, 719, 0xFF0000, 0)
FileAppend("PixelSearch red rc=" r " at (" found "," fy ")`n", "/tmp/xw_img5.txt")
; points the Xvfb suite expects to be red: window at (50,60), fill at +10+20
for pt in [[60,80],[61,81],[90,100],[99,109],[50,60],[70,70]] {
    try {
        c := PixelGetColor(pt[1], pt[2])
        FileAppend("(" pt[1] "," pt[2] ")=" c "`n", "/tmp/xw_img5.txt")
    } catch as e {
        FileAppend("(" pt[1] "," pt[2] ") err`n", "/tmp/xw_img5.txt")
    }
}
ExitApp(0)
EOF
rm -f /tmp/xw_img5.txt
timeout 30 /mnt/f/AI/Codex/Autohotkey_Linux/build-core/source/linux/core/ahk_core $D/find.ahk > $D/run.log 2>&1
echo "runner rc=$?"
cat /tmp/xw_img5.txt 2>/dev/null || echo "no txt"
kill $SWAYPID 2>/dev/null; pkill xwin_helper 2>/dev/null
