#!/bin/bash
# Independent ahk-inputd socket-activation oracle.
#
# Always verifies LISTEN_PID/LISTEN_FDS adoption through the external
# systemd-socket-activate utility. With AHK_SYSTEMD_REAL=1 it additionally
# installs temporary runtime units under /run/systemd/system, proves true
# on-demand start, stable socket ownership across SIGKILL/restart, SO_PEERCRED
# audit logging and complete cleanup. --protocol-only deliberately avoids
# touching real keyboards; run_inputd_oracle.sh separately gates evdev/uinput.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
CC="${CC:-cc}"
DAEMON="$OUT/ahk-inputd-activation"
CLIENT="$OUT/inputd-activation-client"
$CC -O2 -Wall -Wextra -o "$DAEMON" "$ROOT/source/linux/inputd/inputd.c"
$CC -O2 -Wall -Wextra -o "$CLIENT" "$ROOT/tests/oracle/inputd_client.c"
command -v systemd-socket-activate >/dev/null || {
  echo "INPUTD_SOCKET_ACTIVATION_SKIP reason=systemd-socket-activate-not-installed"
  exit 0
}

SOCK=/tmp/ahk-inputd-activation.sock
LOCK="$SOCK.lock"
LOG=/tmp/ahk-inputd-activation.log
CLIENT_LOG=/tmp/ahk-inputd-activation-client.log
launcher_pid=
cleanup_external() {
  if [ -n "$launcher_pid" ]; then
    kill -TERM "$launcher_pid" 2>/dev/null || true
    wait "$launcher_pid" 2>/dev/null || true
  fi
  rm -f "$SOCK" "$LOCK" "$LOG" "$CLIENT_LOG"
}
trap cleanup_external EXIT HUP INT TERM
rm -f "$SOCK" "$LOCK" "$LOG" "$CLIENT_LOG"
systemd-socket-activate -l "$SOCK" "$DAEMON" \
  --socket "$SOCK" --protocol-only -v </dev/null >"$LOG" 2>&1 &
launcher_pid=$!
for _ in $(seq 1 50); do
  [ -S "$SOCK" ] && break
  sleep .05
done
[ -S "$SOCK" ] || { echo "INPUTD_ACTIVATION_LISTEN_FAIL"; cat "$LOG"; exit 1; }
inode_before=$(stat -c %i "$SOCK")
"$CLIENT" "$SOCK" "30:0" --timeout-ms 400 >"$CLIENT_LOG" 2>&1
grep -q '^ACK HELLO ok=1 proto=1$' "$CLIENT_LOG"
grep -q '^ACK SUBSCRIBE ok=1 count=1$' "$CLIENT_LOG"
# Independent fragmentation/coalescing probe: send HELLO byte-by-byte, then
# SUBSCRIBE and PING in one write. The daemon must parse both without blocking.
python3 - "$SOCK" <<'PY'
import socket, struct, sys, time
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(2)
sock.connect(sys.argv[1])
hello = struct.pack("=IBI", 5, 1, 1)
for byte in hello:
    sock.send(bytes((byte,)))
    time.sleep(0.002)
ack = b""
while len(ack) < 6:
    ack += sock.recv(6 - len(ack))
assert ack[0] == 2 and ack[1] == 1 and struct.unpack("=I", ack[2:])[0] == 1
subscribe_payload = struct.pack("=BIIB", 2, 1, 44, 0)
subscribe = struct.pack("=I", len(subscribe_payload)) + subscribe_payload
ping = struct.pack("=IB", 1, 4)
sock.sendall(subscribe + ping)
ack = b""
while len(ack) < 6:
    ack += sock.recv(6 - len(ack))
assert ack[0] == 2 and ack[1] == 1 and struct.unpack("=I", ack[2:])[0] == 1
assert sock.recv(1) == b"\x03"
sock.close()
# SUBSCRIBE before HELLO is a protocol violation and must fail closed.
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(2)
sock.connect(sys.argv[1])
subscribe_payload = struct.pack("=BI", 2, 0)
sock.sendall(struct.pack("=I", len(subscribe_payload)) + subscribe_payload)
assert sock.recv(1) == b""
sock.close()
# A mismatched version receives an explicit negative ACK carrying v1, then EOF.
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(2)
sock.connect(sys.argv[1])
bad = struct.pack("=IBI", 5, 1, 999)
sock.sendall(bad)
ack = b""
while len(ack) < 6:
    ack += sock.recv(6 - len(ack))
assert ack[0] == 2 and ack[1] == 0 and struct.unpack("=I", ack[2:])[0] == 1
assert sock.recv(1) == b""
sock.close()
# Invalid evdev code/suppress fields are rejected without partial rule install.
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(2)
sock.connect(sys.argv[1])
sock.sendall(struct.pack("=IBI", 5, 1, 1))
ack = b""
while len(ack) < 6:
    ack += sock.recv(6 - len(ack))
assert ack[0:2] == b"\x02\x01"
bad_rule = struct.pack("=BIIB", 2, 1, 999999, 2)
sock.sendall(struct.pack("=I", len(bad_rule)) + bad_rule)
assert sock.recv(1) == b""
sock.close()
PY
grep -q "adopted systemd socket $SOCK" "$LOG"
grep -Eq 'client pid=[0-9]+ uid=[0-9]+ gid=[0-9]+ connected' "$LOG"
kill -TERM "$launcher_pid"
wait "$launcher_pid"
launcher_pid=
# An activated descriptor is owned externally; the daemon must never unlink it.
[ -S "$SOCK" ]
inode_after=$(stat -c %i "$SOCK")
[ "$inode_before" = "$inode_after" ]
rm -f "$SOCK" "$LOCK"

real_systemd=false
pid_before=0
pid_after=0
pid_after_idle=0
real_inode=0
if [ "${AHK_SYSTEMD_REAL:-0}" = 1 ]; then
  command -v systemctl >/dev/null || { echo "INPUTD_SYSTEMD_FAIL reason=systemctl-missing"; exit 1; }
  if [ "$(id -u)" = 0 ]; then
    SUDO=()
  else
    SUDO=(sudo -n)
  fi
  "${SUDO[@]}" true
  UNIT_DIR=/run/systemd/system
  SERVICE=ahk-inputd-oracle.service
  SOCKET_UNIT=ahk-inputd-oracle.socket
  REAL_SOCK=/run/ahk-inputd-oracle.sock
  # /run is mounted noexec on common distributions; use an exact temporary
  # libexec path and remove it in the trap.
  REAL_BIN=/usr/local/libexec/ahk-inputd-oracle
  SERVICE_FILE="$UNIT_DIR/$SERVICE"
  SOCKET_FILE="$UNIT_DIR/$SOCKET_UNIT"
  REAL_CLIENT_LOG=/tmp/ahk-inputd-systemd-client.log
  JOURNAL_LOG=/tmp/ahk-inputd-systemd-journal.log
  start_epoch=$(date +%s)
  cleanup_real() {
    "${SUDO[@]}" systemctl stop "$SERVICE" "$SOCKET_UNIT" >/dev/null 2>&1 || true
    "${SUDO[@]}" rm -f "$SERVICE_FILE" "$SOCKET_FILE" "$REAL_BIN" \
      "$REAL_SOCK" "$REAL_SOCK.lock"
    "${SUDO[@]}" systemctl daemon-reload >/dev/null 2>&1 || true
    rm -f "$REAL_CLIENT_LOG" "$JOURNAL_LOG"
  }
  trap 'cleanup_real; cleanup_external' EXIT HUP INT TERM
  cleanup_real
  "${SUDO[@]}" install -d -m 0755 /usr/local/libexec
  "${SUDO[@]}" install -m 0755 "$DAEMON" "$REAL_BIN"
  sed -e 's|/run/ahk-inputd.sock|/run/ahk-inputd-oracle.sock|g' \
      -e 's|SocketMode=0660|SocketMode=0666|g' \
      -e 's|SocketGroup=input|SocketGroup=root|g' \
      "$ROOT/tools/linux/systemd/ahk-inputd.socket" > /tmp/ahk-inputd-oracle.socket
  sed -e 's|ahk-inputd.socket|ahk-inputd-oracle.socket|g' \
      -e 's|/run/ahk-inputd.sock|/run/ahk-inputd-oracle.sock|g' \
      -e "s|@INPUTD_EXEC@|$REAL_BIN|g" \
      -e "s|^ExecStart=.*|ExecStart=$REAL_BIN --socket $REAL_SOCK --protocol-only -v|" \
      "$ROOT/tools/linux/systemd/ahk-inputd.service.in" > /tmp/ahk-inputd-oracle.service
  "${SUDO[@]}" install -m 0644 /tmp/ahk-inputd-oracle.socket "$SOCKET_FILE"
  "${SUDO[@]}" install -m 0644 /tmp/ahk-inputd-oracle.service "$SERVICE_FILE"
  rm -f /tmp/ahk-inputd-oracle.socket /tmp/ahk-inputd-oracle.service
  "${SUDO[@]}" systemctl daemon-reload
  "${SUDO[@]}" systemctl start "$SOCKET_UNIT"
  [ "$("${SUDO[@]}" systemctl is-active "$SOCKET_UNIT")" = active ]
  [ "$("${SUDO[@]}" systemctl show -p MainPID --value "$SERVICE")" = 0 ]
  [ -S "$REAL_SOCK" ]
  [ "$(stat -c %a "$REAL_SOCK")" = 666 ]
  real_inode=$(stat -c %i "$REAL_SOCK")

  "$CLIENT" "$REAL_SOCK" "31:0" --timeout-ms 400 >"$REAL_CLIENT_LOG" 2>&1
  grep -q '^ACK HELLO ok=1 proto=1$' "$REAL_CLIENT_LOG"
  grep -q '^ACK SUBSCRIBE ok=1 count=1$' "$REAL_CLIENT_LOG"
  for _ in $(seq 1 50); do
    pid_before=$("${SUDO[@]}" systemctl show -p MainPID --value "$SERVICE")
    [ "$pid_before" -gt 0 ] 2>/dev/null && break
    sleep .05
  done
  [ "$pid_before" -gt 0 ]
  "${SUDO[@]}" systemctl kill --kill-whom=main --signal=KILL "$SERVICE"
  for _ in $(seq 1 100); do
    pid_after=$("${SUDO[@]}" systemctl show -p MainPID --value "$SERVICE")
    if [ "$pid_after" -gt 0 ] 2>/dev/null && [ "$pid_after" != "$pid_before" ]; then
      break
    fi
    sleep .05
  done
  [ "$pid_after" -gt 0 ]
  [ "$pid_after" != "$pid_before" ]
  [ "$(stat -c %i "$REAL_SOCK")" = "$real_inode" ]
  "$CLIENT" "$REAL_SOCK" "32:0" --timeout-ms 400 >"$REAL_CLIENT_LOG" 2>&1
  grep -q '^ACK HELLO ok=1 proto=1$' "$REAL_CLIENT_LOG"
  # Once the last client leaves, a socket-activated daemon must release all
  # grabs and exit normally; the socket itself remains active for demand start.
  for _ in $(seq 1 160); do
    [ "$("${SUDO[@]}" systemctl show -p MainPID --value "$SERVICE")" = 0 ] && break
    sleep .05
  done
  [ "$("${SUDO[@]}" systemctl show -p MainPID --value "$SERVICE")" = 0 ]
  [ "$("${SUDO[@]}" systemctl is-active "$SOCKET_UNIT")" = active ]
  [ "$(stat -c %i "$REAL_SOCK")" = "$real_inode" ]
  "$CLIENT" "$REAL_SOCK" "33:0" --timeout-ms 400 >"$REAL_CLIENT_LOG" 2>&1
  grep -q '^ACK HELLO ok=1 proto=1$' "$REAL_CLIENT_LOG"
  for _ in $(seq 1 50); do
    pid_after_idle=$("${SUDO[@]}" systemctl show -p MainPID --value "$SERVICE")
    [ "$pid_after_idle" -gt 0 ] 2>/dev/null && break
    sleep .05
  done
  [ "$pid_after_idle" -gt 0 ]
  [ "$pid_after_idle" != "$pid_after" ]
  "${SUDO[@]}" journalctl -u "$SERVICE" --since "@$start_epoch" --no-pager >"$JOURNAL_LOG"
  [ "$(grep -c 'adopted systemd socket' "$JOURNAL_LOG")" -ge 3 ]
  grep -Eq 'client pid=[0-9]+ uid=[0-9]+ gid=[0-9]+ connected' "$JOURNAL_LOG"

  "${SUDO[@]}" systemctl stop "$SERVICE" "$SOCKET_UNIT"
  [ ! -S "$REAL_SOCK" ]
  real_systemd=true
  cleanup_real
fi

cat >"$OUT/inputd-socket-activation-summary.json" <<EOF
{"schema":1,"result":"pass","external_launcher":true,"listen_fds_adopted":true,"fragmented_header":true,"coalesced_frames":true,"hello_required":true,"version_mismatch_rejected":true,"invalid_rules_rejected":true,"peercred_logged":true,"activated_socket_not_unlinked":true,"real_systemd":$real_systemd,"restart_after_sigkill":$real_systemd,"idle_exit_releases_grabs":$real_systemd,"demand_restart_after_idle":$real_systemd,"socket_inode_stable":true,"pid_before":$pid_before,"pid_after_sigkill":$pid_after,"pid_after_idle":$pid_after_idle}
EOF
trap - EXIT HUP INT TERM
cleanup_external
echo "INPUTD_SOCKET_ACTIVATION_ORACLE_PASS real_systemd=$real_systemd summary=$OUT/inputd-socket-activation-summary.json"
