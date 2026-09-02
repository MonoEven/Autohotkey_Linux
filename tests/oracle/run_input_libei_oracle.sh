#!/bin/bash
# M6a optional libei sender + EIS capability/keymap/outcome oracle.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
AHK="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$AHK" in /*) ;; *) AHK="$ROOT/$AHK" ;; esac
OUT="$ROOT/tests/oracle/out"; mkdir -p "$OUT"
WORK=/tmp/input-libei; rm -rf "$WORK"; mkdir -p "$WORK"
SERVER="$WORK/libeis-test-server"
if ! pkg-config --exists libeis-1.0 libei-1.0 liboeffis-1.0; then
  echo "INPUT_LIBEI_ORACLE_SKIP optional development packages unavailable"
  exit 0
fi
if [ "${AHK_LIBEI_EXPECT_PING:-}" = 1 ] \
  || pkg-config --atleast-version=1.4.0 libei-1.0; then
  HAVE_PING=1; EXPECTED_OUTCOME=eis-processed
else
  HAVE_PING=0; EXPECTED_OUTCOME=target-delivered-unknown
fi
SERVER_VERSION_CFLAGS=""
if [ "${AHK_LIBEI_EXPECT_PING:-}" = 1 ] \
  || pkg-config --atleast-version=1.4.0 libeis-1.0; then
  SERVER_VERSION_CFLAGS="-DAHK_LIBEIS_HAS_SYNC=1"
fi
cc -O2 -Wall -Wextra $SERVER_VERSION_CFLAGS \
  "$ROOT/tests/oracle/libeis_test_server.c" -o "$SERVER" \
  $(pkg-config --cflags --libs libeis-1.0 xkbcommon)
env -u DISPLAY -u WAYLAND_DISPLAY \
  DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/ahk-no-such-session-bus \
  "$AHK" --diag >"$WORK/diag" 2>&1
grep -Eq '^libei-build  : yes \(libei=[0-9]+\.[0-9]+' "$WORK/diag"
grep -Eq '^libei-state  : idle; portal-v0; keyboard=0 pointer=0 button=0 scroll=0 text=0 keymap=0; outcome=none; target=unknown$' "$WORK/diag"
SOCK="$WORK/eis.sock"; LOG="$WORK/eis.log"; TRACE="$WORK/libei.trace"
cleanup() {
  [ -n "${SPID:-}" ] && kill "$SPID" 2>/dev/null || true
  pkill -9 -f "^$SERVER " 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
"$SERVER" "$SOCK" "$LOG" --split-delay-ms 250 --caps-locked 1 \
  >"$WORK/server.out" 2>&1 & SPID=$!
for _ in $(seq 1 150); do [ -S "$SOCK" ] && grep -q EIS_READY "$LOG" 2>/dev/null && break; sleep .02; done
[ -S "$SOCK" ]
cat >"$WORK/libei.ahk" <<'EOF'
#Requires AutoHotkey v2.0
OUT := A_Args[1]
try {
    SendEvent("a{Enter}{Click 7 9 Left}{Click WheelDown}")
    Click("11 13 Right")
    MouseMove(5, -3, 0, "R")
    Sleep(600)
    h := HotkeyBackendGet()
    FileAppend("send=ok state=" h.libei_state " build=" h.libei_build_enabled
        " keyboard=" h.libei_keyboard " pointer=" h.libei_pointer
        " button=" h.libei_button " scroll=" h.libei_scroll
        " text=" h.libei_text " keymap=" h.libei_keymap
        " keymap_generation=" h.libei_keymap_generation
        " outcome=" h.libei_outcome " delivery=" h.libei_target_delivery
        " consumption=" h.libei_target_consumption "`n", OUT)
} catch Error as e {
    FileAppend("send=error message=" e.Message "`n", OUT)
    ExitApp(7)
}
SendEvent("^@")
try {
    SendText("你")
    FileAppend("unicode=unexpected-success`n", OUT)
    ExitApp(8)
} catch Error as e {
    FileAppend("unicode=unsupported message=" e.Message "`n", OUT)
}
try {
    SendEvent("^你")
    FileAppend("unicode_mod=unsupported message=explicit-keymap-error`n", OUT)
} catch Error as e {
    FileAppend("unicode_mod=unsupported message=" e.Message "`n", OUT)
}
; Parser-only directives are a valid zero-event transaction.
SendEvent("{Blind}")
Sleep(200)
ExitApp(0)
EOF
unset DISPLAY WAYLAND_DISPLAY
# Run with a live X DISPLAY: required mode must still choose EIS and must not
# bypass the consent contract through XTEST/XWayland.
timeout -k 2s 25s xvfb-run -a env XDG_SESSION_TYPE=wayland AHK_LIBEI=required \
  AHK_LIBEI_SOCKET="$SOCK" AHK_LIBEI_TRACE="$TRACE" "$AHK" "$WORK/libei.ahk" "$WORK/result" \
  >"$WORK/ahk.log" 2>&1
if [ "$HAVE_PING" = 1 ]; then
  for _ in $(seq 1 100); do grep -q EIS_SYNC "$LOG" 2>/dev/null && break; sleep .02; done
fi
result=$(cat "$WORK/result")
grep -q '^send=ok state=ready build=1 keyboard=1 pointer=1 button=1 scroll=1 text=0 keymap=1 ' "$WORK/result"
grep -q "outcome=$EXPECTED_OUTCOME delivery=unknown consumption=unknown" "$WORK/result"
grep -q '^unicode=unsupported ' "$WORK/result"
grep -q '^unicode_mod=unsupported ' "$WORK/result"
# The rejected non-ASCII char must not strand the explicit Ctrl modifier.
grep -q '^EIS_KEY name=keyboard-delayed code=29 down=1$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=29 down=0$' "$LOG"
grep -q '^EIS_CONNECT sender=1 name=AutoHotkey Linux oracle$' "$LOG"
grep -q '^EIS_DEVICE name=pointer-first keyboard=0 pointer=1 button=1 scroll=1 text=0$' "$LOG"
grep -q '^EIS_DEVICE name=keyboard-delayed keyboard=1 pointer=0 button=0 scroll=0 text=0$' "$LOG"
grep -q '^EIS_DELAYED_KEYBOARD_ADDED$' "$LOG"
grep -Eq '^EIS_MODIFIERS name=keyboard-delayed locked=[1-9][0-9]* group=0$' "$LOG"
test "$(grep -c '^EIS_START name=' "$LOG")" -ge 2
grep -q '"emulation_sequence":1' "$TRACE"
grep -q '"emulation_sequence":2' "$TRACE"
# CapsLock is reported locked by EIS, so producing lowercase 'a' requires a
# temporary Shift press around KEY_A on this specific keyboard.
grep -q '^EIS_KEY name=keyboard-delayed code=42 down=1$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=30 down=1$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=30 down=0$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=42 down=0$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=28 down=1$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=28 down=0$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=29 down=1$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=29 down=0$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=3 down=1$' "$LOG"
grep -q '^EIS_KEY name=keyboard-delayed code=3 down=0$' "$LOG"
grep -q '^EIS_BUTTON name=pointer-first code=272 down=1$' "$LOG"
grep -q '^EIS_BUTTON name=pointer-first code=272 down=0$' "$LOG"
grep -q '^EIS_BUTTON name=pointer-first code=273 down=1$' "$LOG"
grep -q '^EIS_BUTTON name=pointer-first code=273 down=0$' "$LOG"
grep -q '^EIS_SCROLL name=pointer-first x=0 y=120$' "$LOG"
grep -q '^EIS_MOTION name=pointer-first dx=7.000 dy=9.000$' "$LOG"
grep -q '^EIS_MOTION name=pointer-first dx=4.000 dy=4.000$' "$LOG"
grep -q '^EIS_MOTION name=pointer-first dx=5.000 dy=-3.000$' "$LOG"
if [ "$HAVE_PING" = 1 ]; then grep -q '^EIS_SYNC$' "$LOG"; fi
python3 - "$LOG" <<'PY'
import pathlib,sys
lines=pathlib.Path(sys.argv[1]).read_text().splitlines()
def idx(prefix,start=0):
    return next(i for i,s in enumerate(lines[start:],start) if s.startswith(prefix))
pointer=idx('EIS_DEVICE name=pointer-first')
keyboard=idx('EIS_DEVICE name=keyboard-delayed')
assert pointer < keyboard
sd=idx('EIS_KEY name=keyboard-delayed code=42 down=1')
kd=idx('EIS_KEY name=keyboard-delayed code=30 down=1',sd+1)
kdf=idx('EIS_FRAME',kd+1)
ku=idx('EIS_KEY name=keyboard-delayed code=30 down=0',kdf+1)
su=idx('EIS_KEY name=keyboard-delayed code=42 down=0',ku+1)
kuf=idx('EIS_FRAME',su+1)
assert sd < kd < kdf < ku < su < kuf
bd=idx('EIS_BUTTON name=pointer-first code=272 down=1')
bdf=idx('EIS_FRAME',bd+1)
bu=idx('EIS_BUTTON name=pointer-first code=272 down=0',bdf+1)
buf=idx('EIS_FRAME',bu+1)
assert bd < bdf < bu < buf
PY
python3 - "$TRACE" "$OUT/input-libei-summary.json" "$HAVE_PING" <<'PY'
import json,pathlib,sys
rows=[json.loads(x) for x in pathlib.Path(sys.argv[1]).read_text().splitlines() if x]
sub=[r for r in rows if r.get('outcome')=='submitted-to-libei']
processed=[r for r in rows if r.get('outcome')=='eis-processed']
have_ping=sys.argv[3]=='1'
assert sub and (processed if have_ping else True),(sub,processed)
assert all(r.get('target_delivered')=='unknown' and r.get('target_consumed')=='unknown' for r in rows),rows
assert all(r.get('metadata_scope')=='session-sideband' for r in rows),rows
assert any(r.get('send_level')==0 and r.get('transport')=='event' for r in sub),sub
transactions={r['transaction_id'] for r in sub if r['transaction_id']}
assert transactions
if have_ping:
 assert any(r['transaction_id'] in transactions for r in processed)
 assert all(r.get('send_level')==0 and r.get('transport') in ('event','pointer') for r in processed),processed
outcomes=['submitted-to-libei'] + (['eis-processed'] if have_ping else ['target-delivered-unknown'])
summary={'schema':1,'result':'pass','libei_wire':True,'keyboard':True,
 'pointer':True,'button':True,'scroll':True,'text_capability':False,
 'keymap_fallback':True,'per_device_modifier_state':True,
 'held_modifier_keymap':True,
 'unicode_unrepresentable':'explicit-error',
 'outcomes':outcomes,'ping_supported':have_ping,
 'target_delivery':'unknown','target_consumption':'unknown',
 'transactions':len(transactions)}
pathlib.Path(sys.argv[2]).write_text(json.dumps(summary,sort_keys=True)+'\n')
print(json.dumps(summary,sort_keys=True))
PY

# Device removal invalidates the old keymap/device generation. A replacement
# device is rebound, reloaded and starts a new increasing emulation sequence.
kill "$SPID" 2>/dev/null || true; wait "$SPID" 2>/dev/null || true; SPID=""
PSOCK="$WORK/pause.sock"; PLOG="$WORK/pause.log"; PTRACE="$WORK/pause.trace"
"$SERVER" "$PSOCK" "$PLOG" --replace-after-frame 2 \
  >"$WORK/pause-server.out" 2>&1 & SPID=$!
for _ in $(seq 1 150); do [ -S "$PSOCK" ] && break; sleep .02; done
cat >"$WORK/pause.ahk" <<'EOF'
#Requires AutoHotkey v2.0
SendEvent("a")
Sleep(650)
SendEvent("b")
Sleep(350)
h := HotkeyBackendGet()
FileAppend("state=" h.libei_state " sequence=" h.libei_emulation_sequence
    " outcome=" h.libei_outcome "`n", A_Args[1])
ExitApp(h.libei_state = "ready" && h.libei_emulation_sequence >= 2 ? 0 : 7)
EOF
timeout -k 2s 20s env XDG_SESSION_TYPE=tty AHK_LIBEI=required \
  AHK_LIBEI_SOCKET="$PSOCK" AHK_LIBEI_TRACE="$PTRACE" "$AHK" "$WORK/pause.ahk" "$WORK/pause-result" \
  >"$WORK/pause-ahk.log" 2>&1
grep -q '^EIS_REMOVE$' "$PLOG"
grep -q '^EIS_REPLACE$' "$PLOG"
test "$(grep -c '^EIS_DEVICE name=' "$PLOG")" -ge 2
test "$(grep -c '^EIS_START name=' "$PLOG")" -ge 2
grep -q '^EIS_KEY name=combined code=30 down=1$' "$PLOG"
grep -q '^EIS_KEY name=combined-replacement code=48 down=1$' "$PLOG"
grep -Eq "^state=ready sequence=[2-9][0-9]* outcome=$EXPECTED_OUTCOME$" "$WORK/pause-result"
grep -q '"event":"device-removed"' "$PTRACE"
grep -q '"emulation_sequence":2.*"event":"device-resumed"' "$PTRACE"
python3 - "$PLOG" "$PTRACE" <<'PY'
import json,pathlib,sys
lines=pathlib.Path(sys.argv[1]).read_text().splitlines()
a=next(i for i,s in enumerate(lines) if s.startswith('EIS_KEY name=combined code=30 down=1'))
rm=lines.index('EIS_REMOVE')
b=next(i for i,s in enumerate(lines) if s.startswith('EIS_KEY name=combined-replacement code=48 down=1'))
assert a < rm < b
rows=[json.loads(x) for x in pathlib.Path(sys.argv[2]).read_text().splitlines()]
starts=[r['emulation_sequence'] for r in rows if r.get('stage')=='lifecycle' and r.get('event')=='device-resumed']
assert len(starts)>=2 and starts==sorted(starts) and len(set(starts))==len(starts),starts
PY

# Replacement during PressDuration must not reuse the array slot/new device
# for the old device's key-up. The first submitted down remains target-unknown,
# while the transaction fails explicitly and no cross-generation up is sent.
kill "$SPID" 2>/dev/null || true; wait "$SPID" 2>/dev/null || true; SPID=""
MSOCK="$WORK/midpress.sock"; MLOG="$WORK/midpress.log"
"$SERVER" "$MSOCK" "$MLOG" --replace-after-frame 1 >"$WORK/midpress-server.out" 2>&1 & SPID=$!
for _ in $(seq 1 150); do [ -S "$MSOCK" ] && break; sleep .02; done
cat >"$WORK/midpress.ahk" <<'EOF'
#Requires AutoHotkey v2.0
SetKeyDelay(0, 250)
try {
    SendEvent("a")
    FileAppend("unexpected-success`n", A_Args[1])
    ExitApp(9)
} catch Error as e {
    h := HotkeyBackendGet()
    FileAppend("explicit-error outcome=" h.libei_outcome " message=" e.Message "`n", A_Args[1])
    ; EIS neutralized the old down. This cleanup release must be consumed,
    ; never sent to the replacement keyboard.
    SendEvent("{a up}")
    matched := InStr(e.Message, "device removed during transaction")
        || InStr(e.Message, "replaced during PressDuration")
    ExitApp(matched ? 0 : 8)
}
EOF
timeout -k 2s 15s env XDG_SESSION_TYPE=tty AHK_LIBEI=required \
  AHK_LIBEI_SOCKET="$MSOCK" "$AHK" "$WORK/midpress.ahk" \
  "$WORK/midpress-result" >"$WORK/midpress-ahk.log" 2>&1
grep -q '^explicit-error outcome=failed ' "$WORK/midpress-result"
grep -q '^EIS_KEY name=combined code=30 down=1$' "$MLOG"
! grep -q '^EIS_KEY name=combined-replacement code=30 down=0$' "$MLOG"
! grep -q unexpected-success "$WORK/midpress-result"

# Partial grant: aggregate READY on a keyboard-only device must not make a
# coordinate drag silently succeed. The operation waits for pointer capability,
# preserves that first failure across later button phases and never falls back.
kill "$SPID" 2>/dev/null || true; wait "$SPID" 2>/dev/null || true; SPID=""
CSOCK="$WORK/caps.sock"; CLOG="$WORK/caps.log"
"$SERVER" "$CSOCK" "$CLOG" --device-mask 1 >"$WORK/caps-server.out" 2>&1 & SPID=$!
for _ in $(seq 1 150); do [ -S "$CSOCK" ] && break; sleep .02; done
cat >"$WORK/caps.ahk" <<'EOF'
#Requires AutoHotkey v2.0
try {
    MouseClickDrag("Left", 2, 3, 10, 10)
    FileAppend("unexpected-success`n", A_Args[1])
    ExitApp(9)
} catch Error as e {
    h := HotkeyBackendGet()
    FileAppend("explicit-error state=" h.libei_state " outcome=" h.libei_outcome
        " reason=" h.libei_reason " message=" e.Message "`n", A_Args[1])
    ExitApp(InStr(e.Message, "pointer capability unavailable") ? 0 : 8)
}
EOF
timeout -k 2s 15s env XDG_SESSION_TYPE=tty AHK_LIBEI=required \
  AHK_LIBEI_SOCKET="$CSOCK" AHK_LIBEI_DEVICE_TIMEOUT_MS=100 "$AHK" "$WORK/caps.ahk" \
  "$WORK/caps-result" >"$WORK/caps-ahk.log" 2>&1
grep -q '^EIS_DEVICE name=combined keyboard=1 pointer=0 button=0 scroll=0 text=0$' "$CLOG"
grep -q '^explicit-error state=ready outcome=failed reason=EIS pointer capability unavailable' "$WORK/caps-result"
! grep -q '^EIS_MOTION\|^EIS_BUTTON' "$CLOG"
! grep -q unexpected-success "$WORK/caps-result"

# Sticky compound failure still neutralizes a held button on its owning device:
# pointer and button are separate; pointer disappears after button-down, so the
# second motion fails but button-up must still be sent to button-only.
kill "$SPID" 2>/dev/null || true; wait "$SPID" 2>/dev/null || true; SPID=""
GSOCK="$WORK/drag.sock"; GLOG="$WORK/drag.log"
"$SERVER" "$GSOCK" "$GLOG" --split-drag 1 --remove-after-frame 2 \
  >"$WORK/drag-server.out" 2>&1 & SPID=$!
for _ in $(seq 1 150); do [ -S "$GSOCK" ] && break; sleep .02; done
cat >"$WORK/drag.ahk" <<'EOF'
#Requires AutoHotkey v2.0
MouseMove(2, 3)
MouseClick("Left",,, 1,, "D")
Sleep(350)
failed := false
try MouseMove(8, 7)
catch Error as e {
    failed := InStr(e.Message, "pointer capability unavailable") != 0
}
MouseClick("Left",,, 1,, "U")
FileAppend("motion_failed=" failed " release=done`n", A_Args[1])
ExitApp(failed ? 0 : 9)
EOF
timeout -k 2s 15s env XDG_SESSION_TYPE=tty AHK_LIBEI=required \
  AHK_LIBEI_SOCKET="$GSOCK" AHK_LIBEI_DEVICE_TIMEOUT_MS=100 \
  "$AHK" "$WORK/drag.ahk" "$WORK/drag-result" >"$WORK/drag-ahk.log" 2>&1
grep -q '^motion_failed=1 release=done$' "$WORK/drag-result"
grep -q '^EIS_MOTION name=pointer-only dx=2.000 dy=3.000$' "$GLOG"
grep -q '^EIS_BUTTON name=button-only code=272 down=1$' "$GLOG"
grep -q '^EIS_REMOVE_ONLY$' "$GLOG"
grep -q '^EIS_BUTTON name=button-only code=272 down=0$' "$GLOG"
test "$(grep -c '^EIS_MOTION name=pointer-only' "$GLOG")" = 1

# A portal-owned EIS disconnect invalidates every device generation and enters
# REAUTH_REQUIRED. Required mode must reject subsequent input, never fall back.
kill "$SPID" 2>/dev/null || true; wait "$SPID" 2>/dev/null || true; SPID=""
DSOCK="$WORK/disconnect.sock"; DLOG="$WORK/disconnect.log"
"$SERVER" "$DSOCK" "$DLOG" --disconnect-after-frame 2 \
  >"$WORK/disconnect-server.out" 2>&1 & SPID=$!
for _ in $(seq 1 150); do [ -S "$DSOCK" ] && break; sleep .02; done
cat >"$WORK/disconnect.ahk" <<'EOF'
#Requires AutoHotkey v2.0
SendEvent("a")
Sleep(400)
h := HotkeyBackendGet()
try {
    SendEvent("b")
    FileAppend("unexpected-success state=" h.libei_state "`n", A_Args[1])
    ExitApp(9)
} catch Error as e {
    h := HotkeyBackendGet()
    FileAppend("explicit-error state=" h.libei_state " reason=" h.libei_reason
        " message=" e.Message "`n", A_Args[1])
    ExitApp(h.libei_state = "reauth-required" ? 0 : 8)
}
EOF
timeout -k 2s 15s env XDG_SESSION_TYPE=tty AHK_LIBEI=required \
  AHK_LIBEI_SOCKET="$DSOCK" AHK_LIBEI_TEST_PORTAL_SESSION=1 \
  AHK_LIBEI_TRACE="$WORK/disconnect.trace" "$AHK" "$WORK/disconnect.ahk" \
  "$WORK/disconnect-result" >"$WORK/disconnect-ahk.log" 2>&1
grep -q '^EIS_FORCED_DISCONNECT$' "$DLOG"
grep -q '^explicit-error state=reauth-required ' "$WORK/disconnect-result"
if [ "$HAVE_PING" = 1 ]; then
  ! grep -q '"event":"pong","outcome":"eis-processed"' "$WORK/disconnect.trace"
  if grep -q 'EI_EVENT_PONG' "$WORK/disconnect.trace"; then
    grep -q '"event":"pong-discarded-disconnect"' "$WORK/disconnect.trace"
  fi
fi
! grep -q unexpected-success "$WORK/disconnect-result"
kill "$SPID" 2>/dev/null || true; wait "$SPID" 2>/dev/null || true; SPID=""

# A late pong for transaction N must not overwrite transaction N+1's local
# FAILED outcome. This branch exists only where public ping is available.
if [ "$HAVE_PING" = 1 ]; then
  LSOCK="$WORK/late-pong.sock"; LLOG="$WORK/late-pong.log"
  "$SERVER" "$LSOCK" "$LLOG" --sync-delay-ms 300 >"$WORK/late-pong-server.out" 2>&1 & SPID=$!
  for _ in $(seq 1 150); do [ -S "$LSOCK" ] && break; sleep .02; done
  cat >"$WORK/late-pong.ahk" <<'EOF'
#Requires AutoHotkey v2.0
SendEvent("a")
try SendText("你")
catch Error {
}
Sleep(800)
h := HotkeyBackendGet()
FileAppend("outcome=" h.libei_outcome " transaction=" h.libei_last_transaction "`n", A_Args[1])
ExitApp(h.libei_outcome = "failed" && h.libei_last_transaction >= 2 ? 0 : 7)
EOF
  timeout -k 2s 15s env XDG_SESSION_TYPE=tty AHK_LIBEI=required \
    AHK_LIBEI_SOCKET="$LSOCK" "$AHK" "$WORK/late-pong.ahk" \
    "$WORK/late-pong-result" >"$WORK/late-pong-ahk.log" 2>&1
  grep -Eq '^outcome=failed transaction=[2-9][0-9]*$' "$WORK/late-pong-result"
  kill "$SPID" 2>/dev/null || true; wait "$SPID" 2>/dev/null || true; SPID=""
fi

# Required mode must fail explicitly and must not silently fall through to
# virtual-keyboard/uinput when the selected EIS endpoint cannot connect.
cat >"$WORK/required-fail.ahk" <<'EOF'
#Requires AutoHotkey v2.0
try {
    SendEvent("a")
    FileAppend("unexpected-success`n", A_Args[1])
    ExitApp(9)
} catch Error as e {
    h := HotkeyBackendGet()
    FileAppend("explicit-error state=" h.libei_state " errno=" h.libei_last_errno
        " reason=" h.libei_reason " message=" e.Message "`n", A_Args[1])
    ExitApp(0)
}
EOF
timeout -k 2s 10s env XDG_SESSION_TYPE=tty AHK_LIBEI=required \
  AHK_LIBEI_SOCKET="$WORK/missing.sock" AHK_LIBEI_CONSENT_TIMEOUT_MS=150 \
  "$AHK" "$WORK/required-fail.ahk" "$WORK/fail-result" >"$WORK/fail-ahk.log" 2>&1
grep -q '^explicit-error state=disconnected ' "$WORK/fail-result"
! grep -q unexpected-success "$WORK/fail-result"
rm -f "$WORK/force-result"
timeout -k 2s 10s env XDG_SESSION_TYPE=tty AHK_LIBEI=force \
  AHK_LIBEI_SOCKET="$WORK/missing-force.sock" AHK_LIBEI_CONSENT_TIMEOUT_MS=150 \
  "$AHK" "$WORK/required-fail.ahk" "$WORK/force-result" >"$WORK/force-ahk.log" 2>&1
grep -q '^explicit-error state=disconnected ' "$WORK/force-result"
# Auto mode may fall back when libei is unavailable/not-built/portal-v1, but a
# selected EIS route which fails must not bypass portal consent through uinput.
rm -f "$WORK/auto-fail-result"
timeout -k 2s 10s env XDG_SESSION_TYPE=tty AHK_LIBEI=1 \
  AHK_LIBEI_SOCKET="$WORK/missing-auto.sock" AHK_LIBEI_CONSENT_TIMEOUT_MS=150 "$AHK" "$WORK/required-fail.ahk" \
  "$WORK/auto-fail-result" >"$WORK/auto-fail-ahk.log" 2>&1
grep -q '^explicit-error state=disconnected ' "$WORK/auto-fail-result"
! grep -q unexpected-success "$WORK/auto-fail-result"
python3 - "$OUT/input-libei-summary.json" <<'PY'
import json,pathlib,sys
p=pathlib.Path(sys.argv[1]); d=json.loads(p.read_text())
d.update({'device_replacement':True,'client_emulation_sequence_increases':True,
 'midpress_replacement':'explicit-error','neutralized_release':True,
 'partial_grant':'explicit-error','compound_mouse_failure_sticky':True,
 'top_level_click':True,'parser_only_send':'success',
 'portal_disconnect':'reauth-required','required_route_failure':'explicit-error',
 'silent_privileged_fallback':False,'required_overrides_xwayland':True,
 'stale_pong_preserves_newer_failure':d.get('ping_supported',False),
 'libei_tested_version':
 __import__('subprocess').check_output(['pkg-config','--modversion','libei-1.0'],text=True).strip()})
p.write_text(json.dumps(d,sort_keys=True)+'\n')
print(json.dumps(d,sort_keys=True))
PY
echo "INPUT_LIBEI_ORACLE_PASS"
