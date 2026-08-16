#!/bin/bash
# Debug v3: dump screen pixels via screencopy to locate the red rect.
set -x
D=/var/tmp/xwimg3
rm -rf $D; mkdir -p $D /tmp/.X11-unix
mount -o remount,rw /tmp/.X11-unix 2>/dev/null; chmod 1777 /tmp/.X11-unix 2>/dev/null
pkill -x sway; pkill -x Xwayland; sleep 1
cat > $D/config <<EOF
seat * xcursor_theme empty
input "*" xkb_layout us
output "*" bg #000000 solid_color
xwayland enable
for_window [app_id="DocCheck"] floating enable
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
cat > $D/pix.ahk <<'EOF'
#Requires AutoHotkey v2.0
for pt in [[10,45],[20,55],[30,65],[50,80],[200,300],[640,360],[0,100],[100,0],[300,200]] {
    try {
        c := PixelGetColor(pt[1], pt[2])
        FileAppend("(" pt[1] "," pt[2] ")=" c "`n", "/tmp/xw_img3.txt")
    } catch as e {
        FileAppend("(" pt[1] "," pt[2] ") err: " Type(e) "`n", "/tmp/xw_img3.txt")
    }
}
ExitApp(0)
EOF
rm -f /tmp/xw_img3.txt
timeout 30 /mnt/f/AI/Codex/Autohotkey_Linux/build-core/source/linux/core/ahk_core $D/pix.ahk > $D/run.log 2>&1
echo "runner rc=$?"
cat /tmp/xw_img3.txt 2>/dev/null || echo "no txt"
# where is the window?
SOCK=$(ls $D/sway-ipc.*.sock 2>/dev/null | head -1)
SWAYSOCK=$SOCK swaymsg -t get_tree 2>/dev/null | python3 -c "
import json,sys
def walk(n, d=0):
    print('  '*d, n.get('app_id') or n.get('name'), n.get('rect'))
    for c in n.get('nodes',[]): walk(c, d+1)
    for c in n.get('floating_nodes',[]): walk(c, d+1)
try: walk(json.load(sys.stdin))
except Exception as e: print('tree err', e)"
kill $SWAYPID 2>/dev/null; pkill xwin_helper 2>/dev/null
