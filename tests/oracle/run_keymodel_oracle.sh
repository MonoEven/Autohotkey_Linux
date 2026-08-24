#!/bin/bash
# M1-K behavior oracle: physical scan code survives layout changes, while text
# sending follows the active layout (including AltGr).
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
ORACLE="$OUT/input-oracle"
RECEIVER="$OUT/x11-keysym-receiver"
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/input_oracle.c" -o "$ORACLE" \
  $(pkg-config --cflags --libs x11 xi xtst)
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/x11_keysym_receiver.c" -o "$RECEIVER" \
  $(pkg-config --cflags --libs x11)
ORIGINAL=/tmp/ahk_keymodel_original.xkb
xkbcomp -xkb "$DISPLAY" "$ORIGINAL" 2>/dev/null
restore_layout() { xkbcomp -w 0 "$ORIGINAL" "$DISPLAY" >/dev/null 2>&1 || true; }
trap restore_layout EXIT HUP INT TERM

# sc01E is the physical set-1 A position -> evdev KEY_A(30) -> X keycode 38.
# The deterministic fixture swaps its logical value, but the sc hotkey must
# still fire. Xvfb must be started with -noreset so the server accepts and
# retains a client-installed XKB map.

rm -f /tmp/ahk_sc_ready /tmp/ahk_sc_fired
cat >/tmp/ahk_sc_layout.ahk <<'EOF'
#Requires AutoHotkey v2.0
count := 0
OnPhysicalA(*) {
    global count
    count++
    FileAppend("fire=" count "`n", "/tmp/ahk_sc_fired")
    if count = 2
        ExitApp
}
Hotkey("sc01E", OnPhysicalA)
FileAppend("ready`n", "/tmp/ahk_sc_ready")
SetTimer(() => ExitApp(4), -12000)
EOF
"$BIN" /tmp/ahk_sc_layout.ahk >/tmp/ahk_sc_layout.log 2>&1 &
AHK_PID=$!
for _ in $(seq 1 100); do test -f /tmp/ahk_sc_ready && break; sleep 0.02; done
test -f /tmp/ahk_sc_ready
"$ORACLE" inject-keycode-x11 38 25
for _ in $(seq 1 100); do test "$(grep -c '^fire=' /tmp/ahk_sc_fired 2>/dev/null || true)" -ge 1 && break; sleep 0.02; done
grep -q '^fire=1$' /tmp/ahk_sc_fired
xkbcomp -w 0 "$ROOT/tests/oracle/azerty-altgr-test.xkb" "$DISPLAY" >/tmp/ahk_keymodel_xkbcomp.log 2>&1
sleep 0.5
# Prove the server, not just the fixture source, changed physical KEY_A to q.
xkbcomp -xkb "$DISPLAY" /tmp/ahk_keymodel_active.xkb 2>/dev/null
grep -A3 'key <AC01>' /tmp/ahk_keymodel_active.xkb | grep -q 'q'
"$ORACLE" inject-keycode-x11 38 25
wait "$AHK_PID"
grep -q '^fire=2$' /tmp/ahk_sc_fired

# The same physical injection must enter the character layer as logical 'q',
# proving InputHook no longer decodes with a hard-coded US table. Hotstring's
# raw/passive-grab redesign remains the separate M2 batch.
rm -f /tmp/ahk_layout_input_ready /tmp/ahk_layout_input_pass /tmp/ahk_layout_input_debug
cat >/tmp/ahk_layout_input.ahk <<'EOF'
#Requires AutoHotkey v2.0
ih := InputHook("L1T5")
ih.Start()
FileAppend("ready`n", "/tmp/ahk_layout_input_ready")
ih.Wait()
FileAppend("input=" ih.Input " reason=" ih.EndReason "`n", "/tmp/ahk_layout_input_debug")
if ih.Input = "q"
    FileAppend("logical-q-captured`n", "/tmp/ahk_layout_input_pass")
ExitApp
EOF
"$BIN" /tmp/ahk_layout_input.ahk >/tmp/ahk_layout_input.log 2>&1 &
INPUT_PID=$!
for _ in $(seq 1 500); do test -f /tmp/ahk_layout_input_ready && break; sleep 0.02; done
test -f /tmp/ahk_layout_input_ready
"$ORACLE" inject-keycode-x11 38 25
wait "$INPUT_PID"
grep -q '^logical-q-captured$' /tmp/ahk_layout_input_pass

# In the deterministic AZERTY fixture, EuroSign is available only on an AltGr
# level. The independent focused window decodes what the server delivered; it
# does not consume AHK's internal model.
TRACE=/tmp/ahk_keymodel_fr.jsonl
SUMMARY="$OUT/keymodel-azerty-summary.json"
rm -f "$TRACE" "$SUMMARY"
"$RECEIVER" "$TRACE" EuroSign 8000 >/tmp/ahk_keysym_receiver.log 2>&1 &
REC_PID=$!
for _ in $(seq 1 100); do grep -q '"type":"ready"' "$TRACE" 2>/dev/null && break; sleep 0.02; done
grep -q '"type":"ready"' "$TRACE"
cat >/tmp/ahk_send_fr.ahk <<'EOF'
#Requires AutoHotkey v2.0
Sleep(150)
SendText("€")
ExitApp
EOF
"$BIN" /tmp/ahk_send_fr.ahk
wait "$REC_PID"
python3 "$ROOT/tests/oracle/verify_keymodel_trace.py" "$TRACE" | tee "$SUMMARY"

echo "KEYMODEL_ORACLE_PASS sc01E=us+azerty inputhook=q text=EuroSign/altgr summary=$SUMMARY"
