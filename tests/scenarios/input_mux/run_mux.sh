#!/bin/bash
set -u
AHK="${AHK:?runner must export AHK}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc -O2 -Wall -Wextra -o /tmp/mux-uinput "$ROOT/tools/linux/uinput-inject.c" 2>/dev/null || exit 0
cc -O2 -Wall -Wextra -o /tmp/mux-xoracle "$ROOT/tests/oracle/input_oracle.c" \
  $(pkg-config --cflags --libs x11 xi xtst) 2>/dev/null || exit 0
pkill -9 -f 'mux-uinput' 2>/dev/null
rm -f /tmp/scn_input_mux /tmp/scn_mux_{ready,result,cmd}
cat >/tmp/scn_mux.ahk <<'EOF'
#Requires AutoHotkey v2.0
x11_count := 0
evdev_count := 0
OnX11(*) {
    global x11_count
    x11_count++
}
OnEvdev(*) {
    global evdev_count
    evdev_count++
}
Hotkey("F12", OnX11)
Hotkey("a & b", OnEvdev)
r1 := HotkeyBackendGet("F12")
r2 := HotkeyBackendGet("a & b")
FileAppend("f12=" r1.backend " combo=" r2.backend " mux=" r2.mux "`n", "/tmp/scn_mux_ready")
Check(*) {
    global x11_count, evdev_count
    if x11_count = 1 && evdev_count = 1 {
        FileAppend("x11=1 evdev=1`n", "/tmp/scn_mux_result")
        ExitApp
    }
}
SetTimer(Check, 50)
SetTimer(() => ExitApp(4), -10000)
EOF
/tmp/mux-uinput /tmp/scn_mux_cmd >/tmp/scn_mux_uinput.log 2>&1 & IPID=$!
sleep 1
"$AHK" /tmp/scn_mux.ahk >/tmp/scn_mux_ahk.log 2>&1 & APID=$!
for _ in $(seq 1 600); do test -f /tmp/scn_mux_ready && break; sleep .02; done
if ! grep -q '^f12=x11 combo=evdev mux=x11+evdev$' /tmp/scn_mux_ready 2>/dev/null; then
  kill "$APID" "$IPID" 2>/dev/null
  exit 0
fi
/tmp/mux-xoracle inject-x11 F12 25
sleep .2
echo '30 down' >/tmp/scn_mux_cmd; sleep .2
echo '48 tap' >/tmp/scn_mux_cmd; sleep .2
echo '30 up' >/tmp/scn_mux_cmd
wait "$APID" 2>/dev/null
kill "$IPID" 2>/dev/null
if grep -q '^x11=1 evdev=1$' /tmp/scn_mux_result 2>/dev/null; then
  touch /tmp/scn_input_mux
fi
exit 0
