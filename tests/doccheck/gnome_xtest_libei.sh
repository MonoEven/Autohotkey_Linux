#!/bin/bash
# gnome_xtest_libei.sh -- R1-7 hypothesis verification (check_detail0821 §1.2-D
# path 1 + A.8-1): "Xwayland 23.2+ 已把 XTEST 自动桥接到 libei,现有 XTEST 发送
# 路径可能已经能注入到整个桌面".
#
# Environment under test: GNOME 49 Wayland VM (Ubuntu 24.04, Xwayland 24.1.6).
#
# Design (two-sided so a negative is interpretable):
#   1. Binary evidence: does this Xwayland even link/contain libei?
#   2. Positive control: a raw-Xlib X window (xwin_helper, XSetInputFocus to
#      itself) receives XTEST keystrokes from the AHK send path under
#      Xwayland.  If keys arrive there, the send path is alive.
#   3. Native Wayland target: a Wayland-native terminal (ptyxis) running a
#      read loop writes what it receives to a file; SendText from the X
#      client while it has Wayland focus.  If the text does NOT arrive AND
#      the Xwayland binary has no libei AND no RemoteDesktop/ConnectToEIS
#      traffic appears, the zero-code bridge is absent on this distro.
#
# Result file: /tmp/xtest_libei_result.txt (one fact per line).
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export WAYLAND_DISPLAY=wayland-0
export DISPLAY=:0
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=ubuntu:GNOME
if [ -z "${XAUTHORITY:-}" ]; then
  XAUTH=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
  [ -n "$XAUTH" ] && export XAUTHORITY="$XAUTH"
fi
AHK=${AHK_BIN:-/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core}
XWH=${XWH_BIN:-/tmp/xwin_helper}
RES=/tmp/xtest_libei_result.txt
XEV=/tmp/xtl_x_ev.txt
NATIVE=/tmp/xtl_native.txt
SDONE=/tmp/xtest_libei_send_done.txt
# Self-cleanup of any stale helper from a previous run (bracket trick so the
# pattern never matches this script's own command line).
pkill -9 -f '[x]win_helper' 2>/dev/null || true
pkill -9 -f '[p]tyxis' 2>/dev/null || true
pkill -9 -f '[x]test_libei_send' 2>/dev/null || true
rm -f "$RES" "$XEV" "$NATIVE" "$SDONE"
exec > "$RES" 2>&1

echo "xtest_libei_start=$(date -u +%FT%TZ)"
echo "xwayland_bin=$(/usr/bin/Xwayland -version 2>&1 | head -1)"
echo "xwayland_libei_ldd=$(ldd /usr/bin/Xwayland 2>/dev/null | grep -ciE 'libei|libeis' || true)"
echo "xwayland_libei_strings=$(strings /usr/bin/Xwayland 2>/dev/null | grep -ciE 'ConnectToEIS|libei' || true)"
echo "portal_gs_version=$(gdbus call --session --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop --method org.freedesktop.DBus.Properties.Get org.freedesktop.portal.GlobalShortcuts version 2>&1 | tr -d '\n')"
echo "portal_gnome_ver=$(dpkg-query -W -f='${Version}' xdg-desktop-portal-gnome 2>/dev/null || echo '<none>')"
echo "xauthority=${XAUTHORITY:-<none>}"

# Portal traffic capture (RemoteDesktop = the libei bridge path).
dbus-monitor "interface='org.freedesktop.portal.RemoteDesktop',type='method_call'" \
  > /tmp/xtl_rd_mon.log 2>&1 &
RD_MON=$!
dbus-monitor "type='signal',interface='org.freedesktop.portal.RemoteDesktop'" \
  > /tmp/xtl_rd_sig.log 2>&1 &
RD_SIG=$!
sleep 1

# ---- 1. Positive control: XTEST -> raw-Xlib X window under Xwayland ----
"$XWH" -title XTLPosCtrl -focus -evout "$XEV" -ms 20000 &
XH_PID=$!
sleep 2
rm -f "$SDONE"
"$AHK" /home/mono/xtest_libei_send.ahk posctrl123 > /tmp/xtl_x_send.log 2>&1
sleep 1
kill "$XH_PID" 2>/dev/null
echo "posctrl_send=$(cat "$SDONE" 2>/dev/null | tr -d '\n' || echo '<no send result>')"
if [ -f "$XEV" ]; then
  echo "posctrl_xev_keys=$(grep -c 'k:down' "$XEV" 2>/dev/null || echo 0)"
  echo "posctrl_xev_head=$(head -3 "$XEV" 2>/dev/null | tr '\n' ' ')"
else
  echo "posctrl_xev_keys=0"
  echo "posctrl_xev_head=<no event file>"
fi

# ---- 2. XTEST -> native Wayland terminal (ptyxis) ----
echo "native_run=start"
ptyxis -- bash -c 'IFS= read -r -s -n 8 T; echo "GOT=$T" > /tmp/xtl_native.txt' \
  > /tmp/xtl_ptyxis.log 2>&1 &
PTYXIS_PID=$!
sleep 6
# Record which window actually holds Wayland keyboard focus at send time.
FOCUS_WMCLASS=$(gdbus call --session --dest org.gnome.Shell --object-path /org/gnome/Shell \
  --method org.gnome.Shell.Eval "global.display.get_focus_window() ? global.display.get_focus_window().get_wm_class() : '(none)'" 2>&1 | tr -d '\n')
echo "native_focus_wmclass=$FOCUS_WMCLASS"
rm -f "$SDONE"
"$AHK" /home/mono/xtest_libei_send.ahk wayland1 > /tmp/xtl_native_send.log 2>&1
sleep 2
echo "native_send=$(cat "$SDONE" 2>/dev/null | tr -d '\n' || echo '<no send result>')"
if [ -f "$NATIVE" ]; then
  echo "native_got=$(cat "$NATIVE" | tr -d '\n')"
else
  echo "native_got=<no result>"
fi
[ -n "${PTYXIS_PID:-}" ] && kill "$PTYXIS_PID" 2>/dev/null

# ---- 3. Portal engagement evidence ----
sleep 1
kill "$RD_MON" 2>/dev/null
kill "$RD_SIG" 2>/dev/null
sleep 0.5
RD_METHODS=$(grep -cE 'method_call.*org\.freedesktop\.portal\.RemoteDesktop' /tmp/xtl_rd_mon.log 2>/dev/null || echo 0)
echo "remote_desktop_method_calls=$RD_METHODS"
RD_SIGS=$(grep -cE 'member=' /tmp/xtl_rd_sig.log 2>/dev/null || echo 0)
echo "remote_desktop_signals=$RD_SIGS"

echo "xtest_libei_done=1"
echo "=== END ==="
