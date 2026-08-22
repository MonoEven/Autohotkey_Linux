#!/bin/bash
# vm_s3_real.sh -- verify §3 on the real GNOME Xwayland session (:0).
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export DISPLAY=:0
if [ -z "${XAUTHORITY:-}" ]; then
  XAUTH=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
  [ -n "$XAUTH" ] && export XAUTHORITY="$XAUTH"
fi
AHK=/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core
R=/tmp/xtest_s3_real.txt
exec > "$R" 2>&1
echo "=== --diag on :0 ==="
"$AHK" --diag 2>&1 | grep -E '^(session|input-backend|xi2|clipboard)'
echo "=== devices (xinput) ==="
xinput list 2>&1 | head -12
echo "=== hotkey smoke on :0 (F12 -> writes file, Send F12 via XTEST) ==="
cat > /tmp/s3_smoke.ahk <<'EOF'
#Requires AutoHotkey v2.0
FileAppend("s3:start`n", "/tmp/s3_smoke.txt")
fired := 0
HotkeySmoke(ThisHotkey) {
    global fired
    fired++
}
Hotkey("F12", HotkeySmoke)
Sleep 200
Send("{F12}")
Sleep 300
FileAppend("s3:fired=" fired "`n", "/tmp/s3_smoke.txt")
Hotkey("F12", "Off")
ExitApp
EOF
rm -f /tmp/s3_smoke.txt
"$AHK" /tmp/s3_smoke.ahk 2>&1
cat /tmp/s3_smoke.txt 2>/dev/null
echo "=== smoke done ==="
echo "s3_real_done=1"
