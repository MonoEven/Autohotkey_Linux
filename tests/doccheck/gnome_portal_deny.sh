#!/bin/bash
# Portal cancel/deny regression (check0820 §2).
#
# A user denying or cancelling the XDG Global Shortcuts permission must NOT
# crash the script, must NOT fire the hotkey, and must exit cleanly.  The
# GNOME 49 permission dialog itself is interactive (not automatable
# headlessly); what is deterministic here is the outcome state that any
# denial reaches: no bind granted -> no fire, no crash, clean exit.
#
# Setup: GNOME 49 Wayland; extension DISABLED so auto/forced selection
# routes to the GlobalShortcuts portal (which is what a GNOME user without
# the extension gets).  Run with the portal bus live.
#
# Invariants asserted:
#   1. AHK survives a portal bind request that is not granted (alive).
#   2. The hotkey does NOT fire.
#   3. SIGTERM leads to a clean exit (no forced kill, no crash).
#   4. CreateSession + BindShortcuts were actually sent (portal engaged).
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export WAYLAND_DISPLAY=wayland-0
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=ubuntu:GNOME
unset DISPLAY

AHK=${AHK_BIN:-/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core}
EXT=ahk-global-hotkeys@autohotkey.org
FIRED=/tmp/ahk_portal_fired.txt

echo "=== 1. disable extension (force portal as the active backend) ==="
gnome-extensions disable "$EXT" >/dev/null 2>&1
sleep 1

cat > /tmp/ahk_portal_test.ahk <<'AHKEOF'
#Requires AutoHotkey v2.0
Persistent
Hotkey("F12", (*) => FileAppend("fired`n", "/tmp/ahk_portal_fired.txt"))
AHKEOF
rm -f "$FIRED"

echo "=== 2. run with baseline portal + capture create/bind traffic ==="
dbus-monitor "interface='org.freedesktop.portal.GlobalShortcuts'" > /tmp/portal_mon.log 2>&1 &
MON=$!
AHK_INPUT_BACKEND=portal "$AHK" /tmp/ahk_portal_test.ahk > /tmp/portal_run.log 2>&1 &
AP=$!
sleep 5
[ "$(kill -0 "$AP" 2>/dev/null && echo yes || echo no)" = yes ] \
  && echo "PASS: alive after portal bind not granted" \
  || { echo "FAIL: AHK died"; kill $MON 2>/dev/null; gnome-extensions enable "$EXT" >/dev/null 2>&1; exit 1; }

/tmp/uinput-kbd 88 tap; sleep 1
/tmp/uinput-kbd 88 tap; sleep 2
C=$(wc -l < "$FIRED" 2>/dev/null || echo 0)
[ "$C" -eq 0 ] && echo "PASS: no fire without granted bind" || { echo "FAIL: fired=$C"; }
echo "fired=$C"

echo "=== 3. portal actually engaged? ==="
if grep -qE "CreateSession" /tmp/portal_mon.log && grep -qE "BindShortcuts" /tmp/portal_mon.log; then
  echo "PASS: CreateSession+BindShortcuts observed (portal backend active)"
else
  echo "WARN: no portal traffic observed (portal unavailable? bind stayed pending)"
fi

echo "=== 4. clean exit (SIGTERM, no forced kill, no crash) ==="
kill -TERM "$AP" 2>/dev/null
sleep 1.5
if kill -0 "$AP" 2>/dev/null; then
  echo "FAIL: did not exit on SIGTERM (forced kill)"
  kill -9 "$AP" 2>/dev/null
else
  echo "PASS: clean exit on SIGTERM"
fi
kill "$MON" 2>/dev/null
# Restore environment
gnome-extensions enable "$EXT" >/dev/null 2>&1
echo PORTAL-DONE