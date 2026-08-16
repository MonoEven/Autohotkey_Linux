#!/bin/bash
# Wayland doc-check runner.
#
#   wayland_run.sh [--xwayland] [path-to-ahk_core]
#
# Pure-Wayland mode (default): starts sway with the headless backend
# (XDG_RUNTIME_DIR=/tmp/swayhome, WAYLAND_DISPLAY=wayland-1, no X display)
# and runs assert_wayland.ahk.  sway's bindsym hooks create marker files
# when key events from the port's virtual keyboard reach the compositor,
# giving end-to-end verification of the Wayland input path; swaymsg
# verifies that the ToolTip xdg toplevel is mapped.
#
# --xwayland mode: starts sway with XWayland enabled and runs the X11
# display-dependent suites against DISPLAY=:0 (the XWayland fallback: the
# port prefers X11 whenever a display is available, so the whole X11
# backend must work on top of Wayland too).
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$SCRIPT_DIR" || exit 1

XWAYLAND=0
if [ "$1" = "--xwayland" ]; then
  XWAYLAND=1
  shift
fi
BIN=${1:-build-core/source/linux/core/ahk_core}
case "$BIN" in
  /*) ;;
  *) BIN="$REPO_DIR/$BIN" ;;
esac
mkdir -p out

# sway needs a sticky /tmp/.X11-unix for its XWayland sockets.  Under WSL
# the directory may be WSLg's read-only mount; remount it read-write (root).
mkdir -p /tmp/.X11-unix
mount -o remount,rw /tmp/.X11-unix 2>/dev/null
chmod 1777 /tmp/.X11-unix 2>/dev/null

pkill -x sway 2>/dev/null
pkill -x Xwayland 2>/dev/null
sleep 0.5
rm -rf /tmp/swayhome
mkdir -p /tmp/swayhome
rm -f /tmp/wl_key_a /tmp/wl_sway_tree.txt /tmp/wl_key_sr /tmp/wl_key_cr /tmp/wl_btn3
cat > /tmp/swayhome/config <<EOF
seat * xcursor_theme empty
input "*" xkb_layout us
output "*" bg #000000 solid_color
xwayland $([ "$XWAYLAND" = 1 ] && echo enable || echo disable)
for_window [app_id="DocCheck"] floating enable
for_window [title="ImgMain"] floating enable
for_window [title="ImgMain"] border none
for_window [title="ImgMain"] resize set 300 200
for_window [title="ImgMain"] move position 50 60
bindsym a exec touch /tmp/wl_key_a
bindsym Shift+Return exec touch /tmp/wl_key_sr
bindsym Control+Return exec touch /tmp/wl_key_cr
bindsym button3 exec touch /tmp/wl_btn3
EOF
cd /tmp/swayhome || exit 1
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman XDG_RUNTIME_DIR=/tmp/swayhome \
  sway -c /tmp/swayhome/config > /tmp/sway.log 2>&1 &
SWAYPID=$!
sleep 3
SOCK=$(ls /tmp/swayhome/sway-ipc.*.sock 2>/dev/null | head -1)
printf '#!/bin/sh\nSWAYSOCK=%s swaymsg -t get_tree > /tmp/wl_sway_tree.txt 2>&1\n' "$SOCK" > /tmp/do_swaymsg.sh
chmod +x /tmp/do_swaymsg.sh
# Editor marker for the assert_edit Edit() check (same as run_check.sh).
printf '#!/bin/sh\necho "$@" > /tmp/ahk_dc_edit_marker.txt\n' > /tmp/ahk_edit_marker.sh
chmod +x /tmp/ahk_edit_marker.sh
trap 'kill $SWAYPID 2>/dev/null' EXIT

pass=0; fail=0

# Under WSL, /tmp/.X11-unix may be WSLg's read-only mount, which prevents
# sway from starting XWayland.  Never fall back to an existing DISPLAY:
# that could be the user's real X server (WSLg), and test windows/dialogs
# would pop up on the user's desktop.  Check sway's log instead.
xwayland_ok=0
if [ "$XWAYLAND" = 1 ]; then
  if grep -q "Failed to start Xwayland" /tmp/sway.log; then
    echo "SKIP: XWayland suites (sway could not start Xwayland: /tmp/.X11-unix not writable)"
    echo "=============================="
    echo "WAYLAND DOC-CHECK PASS=0 FAIL=0 (XWayland skipped)"
    exit 0
  fi
  # The XWayland display must be reachable through a socket sway created.
  # Ignore WSLg's X0: XWayland uses the next free number (usually :0 when
  # WSLg is absent, :1 otherwise).
  sleep 1
  for d in 0 1 2 3 4; do
    if [ -S "/tmp/.X11-unix/X${d}" ] && DISPLAY=:${d} timeout 5 xdpyinfo >/dev/null 2>&1; then
      # Confirm the server was started by this sway (its process tree owns
      # the socket; WSLg's X0 is owned by uid 1000 while we run as root).
      if [ "$(stat -c %u "/tmp/.X11-unix/X${d}" 2>/dev/null)" = "$(id -u)" ]; then
        XWAYLAND_DISPLAY=":${d}"
        xwayland_ok=1
        break
      fi
    fi
  done
  if [ "$xwayland_ok" != 1 ]; then
    echo "SKIP: XWayland suites (no XWayland socket from this sway instance found)"
    echo "--- sway.log tail (diagnostics) ---"
    grep -iE 'xwayland|X11|socket|error|fail' /tmp/sway.log | tail -15 || true
    echo "--- /tmp/.X11-unix ---"
    ls -la /tmp/.X11-unix/ 2>/dev/null || true
    echo "=============================="
    echo "WAYLAND DOC-CHECK PASS=0 FAIL=0 (XWayland skipped)"
    exit 0
  fi
fi

run_compare() { # $1 = suite basename, $2 = output file
  local base="$1"
  local outfile="$2"
  local exp="assert_${base}_expect.txt"
  [ -f "$exp" ] || return
  local n=0
  while IFS= read -r line; do
    [ -z "$line" ] && continue
    n=$((n+1))
    local name="${line%%=*}" want="${line#*=}"
    local got="$(grep -E "^${name}=" "$outfile" | head -1 | cut -d= -f2-)"
    if [ "$got" = "$want" ]; then
      pass=$((pass+1))
    else
      fail=$((fail+1))
      echo "FAIL: $base/$name want=[$want] got=[$got]"
    fi
  done < "$exp"
}

if [ "$XWAYLAND" != 1 ]; then
  # Pure Wayland: run the Wayland suite.
  export XDG_RUNTIME_DIR=/tmp/swayhome
  export WAYLAND_DISPLAY=wayland-1
  unset DISPLAY
  cd "$SCRIPT_DIR" || exit 1
  timeout 60 "$BIN" assert_wayland.ahk > out/assert_wayland.txt 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then
    fail=$((fail+1))
    echo "FAIL: assert_wayland (runner exit=$rc)"
  fi
  cp /tmp/ahk_dc_wayland_out.txt out/assert_wayland.txt 2>/dev/null || true
  run_compare wayland out/assert_wayland.txt
else
  # XWayland fallback: the X11 suites must pass on sway's XWayland display.
  #   - assert_image: runs -- ImageSearch/PixelGetColor/PixelSearch read the
  #     screen through wlr-screencopy (XWayland's root has no backing store,
  #     so XGetImage BadMatches); the runner pins the DocCheck window to the
  #     same (50,60) position as the Xvfb suites.
  #   - assert_hotkey: runs -- XGrabKey works through XWayland (key events
  #     injected with Send go through the X grab).
  #   - assert_win/assert_input/assert_monitor: their assertions depend on
  #     WM-free Xvfb semantics (activation, focus, cursor positions); sway
  #     as a window manager owns those, so the assertions do not hold there.
  # The remaining suites (controls, edits, dialogs, messages, shapes) verify
  # that the X11 backend itself works on top of Wayland.
  export DISPLAY="$XWAYLAND_DISPLAY"
  unset WAYLAND_DISPLAY
  export XDG_RUNTIME_DIR=/tmp/swayhome
  cd "$SCRIPT_DIR" || exit 1
  for base in ctrl edit dialog msg shape image hotkey; do
    ahk="assert_${base}.ahk"
    [ -f "$ahk" ] || continue
    timeout 90 "$BIN" "$ahk" > "out/${base}.txt" 2>&1
    rc=$?
    if [ $rc -ne 0 ]; then
      fail=$((fail+1))
      echo "FAIL: assert_${base} (runner exit=$rc)"
      continue
    fi
    cp "/tmp/ahk_dc_${base}_out.txt" "out/${base}.txt" 2>/dev/null || true
    run_compare "$base" "out/${base}.txt"
  done
fi

echo "=============================="
echo "WAYLAND DOC-CHECK PASS=$pass FAIL=$fail"
[ "$fail" = 0 ]
