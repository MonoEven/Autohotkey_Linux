#!/bin/bash
# vm_sni_diag3.sh -- in-process NameHasOwner + ListNames.
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
R=/tmp/xtest_sni_diag3.txt
exec > "$R" 2>&1
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
cat > /tmp/sni4.ahk <<'EOF'
#Requires AutoHotkey v2.0
TraySetIcon("applications-system")
Sleep 2500
ExitApp
EOF
echo "--- run 1 (capture tray diagnostics) ---"
"$AHK" /tmp/sni4.ahk 2>&1 | head -8
echo "--- run 2 (ListNames during run) ---"
"$AHK" /tmp/sni4.ahk >/dev/null 2>&1 &
P=$!
sleep 1.5
dbus-send --session --dest=org.freedesktop.DBus --type=method_call --print-reply \
  /org/freedesktop/DBus org.freedesktop.DBus.ListNames 2>&1 | grep -o 'StatusNotifierItem-[0-9]*-1' | head -3
echo "(list done)"
wait "$P"
echo "sni_diag3_done=1"
