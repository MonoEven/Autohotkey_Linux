#!/bin/bash
# Debug v6: what app_id / class does the XWayland window get?
set -x
D=/var/tmp/xwimg6
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
/mnt/f/AI/Codex/Autohotkey_Linux/tests/doccheck/out/xwin_helper -title ImgMain -class DocCheck -x 50 -y 60 -w 300 -h 200 -fill FF0000 10 20 40 30 &
sleep 2
SOCK=$(ls $D/sway-ipc.*.sock 2>/dev/null | head -1)
SWAYSOCK=$SOCK swaymsg -t get_tree > $D/tree.json 2>&1
python3 - <<'PYEOF'
import json
d = json.load(open("/var/tmp/xwimg6/tree.json"))
def walk(n, depth=0):
    keys = {k: n.get(k) for k in ("name","app_id","pid","shell","window_type")}
    if n.get("pid") or n.get("name") not in ("root","__i3","__i3_scratch"):
        print("  "*depth + str(keys))
    for c in n.get("nodes", []) + n.get("floating_nodes", []):
        walk(c, depth+1)
walk(d)
PYEOF
kill $SWAYPID 2>/dev/null; pkill xwin_helper 2>/dev/null
