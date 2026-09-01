#!/bin/bash
# Independent fake-broker oracle for the ahk_core S2C stream parser. It sends
# HELLO ACK byte-by-byte, then a coalesced SUBSCRIBE ACK + F12 down/up sequence
# split at deliberately awkward boundaries. No evdev/root access is required.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
SOCK=/tmp/ahk-inputd-client-framing.sock
SERVER=/tmp/ahk-inputd-client-framing-server.py
SCRIPT=/tmp/ahk-inputd-client-framing.ahk
RESULT=/tmp/ahk-inputd-client-framing.txt
AHK_LOG=/tmp/ahk-inputd-client-framing.log
SERVER_LOG=/tmp/ahk-inputd-client-framing-server.log
SERVER_PID=
cleanup() {
  [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true
  [ -n "$SERVER_PID" ] && wait "$SERVER_PID" 2>/dev/null || true
  rm -f "$SOCK" "$SERVER" "$SCRIPT" "$RESULT" "$AHK_LOG" "$SERVER_LOG"
}
trap cleanup EXIT HUP INT TERM
cleanup
cat >"$SERVER" <<'PY'
import os, socket, struct, sys, time
path = sys.argv[1]
server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(path)
server.listen(2)
def read_exact(n):
    data = b""
    while len(data) < n:
        part = conn.recv(n - len(data))
        if not part:
            raise RuntimeError("unexpected EOF")
        data += part
    return data
def read_frame():
    length = struct.unpack("=I", read_exact(4))[0]
    return read_exact(length)
# Connection 1: the client leads with protocol v2 (magic "AHK2").  A v1-only
# broker cannot parse that and closes; the client must transparently fall
# back to the legacy v1 HELLO on a fresh connection (check0901 P0-3).
conn, _ = server.accept()
magic = read_exact(4)
assert magic == b"AHK2", magic
conn.close()
conn, _ = server.accept()
hello = read_frame()
assert hello[0] == 1 and struct.unpack("=I", hello[1:])[0] == 1
hello_ack = struct.pack("=BBI", 2, 1, 1)
for byte in hello_ack:
    conn.sendall(bytes((byte,)))
    time.sleep(0.002)
subscribe = read_frame()
assert subscribe[0] == 2
count = struct.unpack("=I", subscribe[1:5])[0]
assert count >= 1
ack = struct.pack("=BBI", 2, 1, count)
down = struct.pack("=BIBQ", 1, 88, 1, 1000000)
up = struct.pack("=BIBQ", 1, 88, 0, 1001000)
payload = ack + down + up
for cut in (2, 5, 9, 17, len(payload)):
    part, payload = payload[:cut], payload[cut:]
    if part:
        conn.sendall(part)
        time.sleep(0.004)
    if not payload:
        break
if payload:
    conn.sendall(payload)
time.sleep(1)
conn.close()
server.close()
PY
cat >"$SCRIPT" <<'EOF'
#Requires AutoHotkey v2.0
OnF12(*) {
    FileAppend("fire`n", A_Args[1])
    ExitApp
}
Hotkey("F12", OnF12)
SetTimer(() => ExitApp(8), -5000)
EOF
python3 "$SERVER" "$SOCK" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 100); do [ -S "$SOCK" ] && break; sleep .02; done
[ -S "$SOCK" ]
AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$SOCK" \
  "$BIN" "$SCRIPT" "$RESULT" >"$AHK_LOG" 2>&1
grep -q '^fire$' "$RESULT"
grep -q '^\[evdev\] broker mode active$' "$AHK_LOG"
wait "$SERVER_PID"
SERVER_PID=
cat >"$OUT/inputd-client-framing-summary.json" <<EOF
{"schema":1,"result":"pass","v2_leading_magic":true,"v1_fallback":true,"hello_ack_fragmented":true,"subscribe_ack_event_coalesced":true,"event_fragmented":true,"f12_fired":true}
EOF
trap - EXIT HUP INT TERM
cleanup
echo "INPUTD_CLIENT_FRAMING_ORACLE_PASS v2-fallback=true fragmented=true coalesced=true"
