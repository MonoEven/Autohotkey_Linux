#!/bin/bash
# vm_sni_full.sh -- full SNI verification: registration + props + menu + click.
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
R=/tmp/xtest_sni_full.txt
exec > "$R" 2>&1
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
cat > /tmp/sni13.ahk <<'EOF'
#Requires AutoHotkey v2.0
TraySetIcon("applications-system")
TraySetIcon("/tmp/custom.png")
Sleep 8000
ExitApp
EOF
"$AHK" /tmp/sni13.ahk > /tmp/sni13_run.log 2>&1 &
PID=$!
sleep 2
SVCE="org.kde.StatusNotifierItem-${PID}-1"
echo "--- watcher list (our item present?) ---"
LIST=$(gdbus call --session --dest org.kde.StatusNotifierWatcher --object-path /StatusNotifierWatcher \
  --method org.freedesktop.DBus.Properties.Get org.kde.StatusNotifierWatcher RegisteredStatusNotifierItems 2>&1)
echo "$LIST" | grep -o ':1\.[0-9]* /StatusNotifierItem' | tail -2
echo "--- NameHasOwner ---"
gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
  --method org.freedesktop.DBus.NameHasOwner "$SVCE" 2>&1
echo "--- IconName (should be 'custom' from /tmp/custom.png) ---"
gdbus call --session --dest "$SVCE" --object-path /StatusNotifierItem \
  --method org.freedesktop.DBus.Properties.Get org.kde.StatusNotifierItem IconName 2>&1
echo "--- GetLayout item count ---"
gdbus call --session --dest "$SVCE" --object-path /MenuBar \
  --method com.canonical.dbusmenu.GetLayout 0 1 "[]" 2>&1 | grep -o "label" | wc -l
echo "--- click 'Exit' (Event id=4 clicked) -> process should exit ---"
gdbus call --session --dest "$SVCE" --object-path /MenuBar \
  --method com.canonical.dbusmenu.Event 4 "clicked" "<null>" 0 2>&1
sleep 3
if kill -0 "$PID" 2>/dev/null; then echo "STILL RUNNING (Exit click not fired)"; kill "$PID" 2>/dev/null; else echo "PROCESS EXITED (Exit click fired)"; fi
wait "$PID" 2>/dev/null
echo "--- script stderr ---"
cat /tmp/sni13_run.log | head -4
echo "sni_full_done=1"
