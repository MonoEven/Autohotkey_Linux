#!/bin/bash
# Debug: XGrabKey behavior on sway's XWayland.
set -x
D=/var/tmp/xwhk
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
cat > $D/hk_min.ahk <<'EOF'
#Requires AutoHotkey v2.0
FileAppend("installing hotkey...`n", "/tmp/xw_hk.txt")
try {
    Hotkey("F6", (*) => FileAppend("F6 fired!`n", "/tmp/xw_hk.txt"))
    FileAppend("hotkey installed`n", "/tmp/xw_hk.txt")
} catch as e {
    FileAppend("hotkey err: " Type(e) " " e.Message "`n", "/tmp/xw_hk.txt")
}
; inject F6 via XTEST (the same path Send uses) and via our own virtual
; keyboard?  XTEST goes through XWayland; let's see if the grab fires.
Sleep(500)
Send("{F6}")
Sleep(800)
FileAppend("done`n", "/tmp/xw_hk.txt")
ExitApp(0)
EOF
rm -f /tmp/xw_hk.txt
timeout 30 /mnt/f/AI/Codex/Autohotkey_Linux/build-core/source/linux/core/ahk_core $D/hk_min.ahk > $D/run.log 2>&1
echo "runner rc=$?"
cat /tmp/xw_hk.txt 2>/dev/null || echo "no txt"
echo "--- sway log key lines ---"
grep -iE "grab|key" $D/sway.log | tail -15
kill $SWAYPID 2>/dev/null
