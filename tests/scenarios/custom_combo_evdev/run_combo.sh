#!/bin/bash
set -u
AHK="${AHK:?runner must export AHK}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
unset DISPLAY WAYLAND_DISPLAY 2>/dev/null || true
pkill -9 -f 'uinput-inject' 2>/dev/null
cc -O2 -Wall -Wextra -o /tmp/uinput-inject "$ROOT/tools/linux/uinput-inject.c" 2>/dev/null || exit 0
rm -f /tmp/scn_custom_combo_evdev /tmp/scn_combo_fired /tmp/scn_combo_ready /tmp/scn_combo_cmd
cat >/tmp/scn_combo.ahk <<'EOF'
#Requires AutoHotkey v2.0
OnDown(*) => FileAppend("down`n", "/tmp/scn_combo_fired")
OnUp(*) => FileAppend("up`n", "/tmp/scn_combo_fired")
OnTilde(*) => FileAppend("tilde`n", "/tmp/scn_combo_fired")
OnSoloE(*) => FileAppend("solo-e`n", "/tmp/scn_combo_fired")
OnEF(*) => FileAppend("e-f`n", "/tmp/scn_combo_fired")
Hotkey("a & b", OnDown)
Hotkey("a & c up", OnUp)
Hotkey("~a & d", OnTilde)
Hotkey("sc012 & sc021", OnEF)
Hotkey("e", OnSoloE)
caps := HotkeyBackendGet("a & b")
FileAppend("ready custom=" caps.custom_combo " backend=" caps.backend "`n", "/tmp/scn_combo_ready")
SetTimer(() => ExitApp(), -12000)
EOF
/tmp/uinput-inject /tmp/scn_combo_cmd >/tmp/scn_combo_inject.log 2>&1 &
IPID=$!
sleep 1
AHK_INPUT_BACKEND=evdev "$AHK" /tmp/scn_combo.ahk >/tmp/scn_combo_ahk.log 2>&1 &
APID=$!
for _ in $(seq 1 500); do test -f /tmp/scn_combo_ready && break; sleep .02; done
if ! grep -q 'custom=1 backend=evdev' /tmp/scn_combo_ready 2>/dev/null; then
  kill "$APID" "$IPID" 2>/dev/null
  exit 0
fi
command_key() { echo "$1 $2" >/tmp/scn_combo_cmd; sleep .20; }
# Extra Ctrl proves custom combos retain wildcard-like modifier semantics.
command_key 29 down
command_key 30 down
command_key 48 tap
command_key 30 up
command_key 29 up
# Key-up combo owns the suffix pair but fires only on C release.
command_key 30 down
command_key 46 tap
command_key 30 up
# A prefix tilde is accepted and does not change callback multiplicity.
command_key 30 down
command_key 32 tap
command_key 30 up
# E alone fires its standalone hotkey on release because it was not used.
command_key 18 tap
# E used as a prefix fires E&F and must not fire standalone E a second time.
command_key 18 down
command_key 33 tap
command_key 18 up
sleep 1
kill "$APID" "$IPID" 2>/dev/null
if test "$(grep -c '^down$' /tmp/scn_combo_fired 2>/dev/null)" -eq 1 \
   && test "$(grep -c '^up$' /tmp/scn_combo_fired 2>/dev/null)" -eq 1 \
   && test "$(grep -c '^tilde$' /tmp/scn_combo_fired 2>/dev/null)" -eq 1 \
   && test "$(grep -c '^solo-e$' /tmp/scn_combo_fired 2>/dev/null)" -eq 1 \
   && test "$(grep -c '^e-f$' /tmp/scn_combo_fired 2>/dev/null)" -eq 1 \
   && test "$(wc -l </tmp/scn_combo_fired)" -eq 5; then
  touch /tmp/scn_custom_combo_evdev
fi
exit 0
