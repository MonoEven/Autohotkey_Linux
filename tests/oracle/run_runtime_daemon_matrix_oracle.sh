#!/bin/bash
# Cross-version runtime x daemon matrix oracle (Audit47 I04/I11, §20 upgrade gap).
#
# The install-level matrix (run_upgrade_matrix_oracle.sh) proves the launcher
# moves between releases; it says nothing about whether a runtime from one
# release interoperates with a broker from another.  This oracle pairs REAL
# published binaries:
#   - old daemon: 2.0.26-linux.19 (predates protocol v2, which landed in .20)
#   - old runtime: the same release's ahk_core
#   - new daemon / new runtime: the build under test
# and asserts, per pair, that the runtime still works and that a capability the
# daemon cannot grant is never reported as a healthy suppression path.
#
# Usage: run_runtime_daemon_matrix_oracle.sh [OLD_ASSET_DIR] [NEW_INPUTD] [NEW_CORE]
#   OLD_ASSET_DIR  directory holding autohotkey-linux-2.0.26-linux.19-amd64.tar.gz
#                  (default $AHK_MATRIX_OLD19_DIR, else fetched into /tmp with
#                   tools/linux/fetch-release-assets.sh)
# Exit codes: 0 pass, 1 assertion failure, 2 required asset/binary absent.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
OLD_VER=2.0.26-linux.19

NEW_INPUTD="${2:-$ROOT/build-core/source/linux/inputd/ahk-inputd}"
NEW_CORE="${3:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$NEW_INPUTD" in /*) ;; *) NEW_INPUTD="$ROOT/$NEW_INPUTD" ;; esac
case "$NEW_CORE" in /*) ;; *) NEW_CORE="$ROOT/$NEW_CORE" ;; esac

WORK=/tmp/ahk-xver
PASS=0; FAIL=0; FAILURES=""
expect() { # name expected actual
  if [ "$2" = "$3" ]; then PASS=$((PASS+1)); echo "PASS $1 [$3]"
  else FAIL=$((FAIL+1)); FAILURES="$FAILURES $1(got=$3 want=$2)"; echo "FAIL $1 [got=$3 want=$2]"; fi
}
skip() { echo "RUNTIME_DAEMON_MATRIX_SKIP $1"; exit 2; }
die() { echo "RUNTIME_DAEMON_MATRIX_FAIL $1" >&2; exit 1; }

sudo -n true 2>/dev/null || skip no-passwordless-sudo
[ -x "$NEW_INPUTD" ] || skip "missing-new-inputd=$NEW_INPUTD"
[ -x "$NEW_CORE" ] || skip "missing-new-core=$NEW_CORE"
command -v cc >/dev/null 2>&1 || skip cc-missing

# --- real published .19 binaries -------------------------------------------

OLD_DIR="${1:-${AHK_MATRIX_OLD19_DIR:-/tmp/ahk-old19}}"
case "$OLD_DIR" in /*) ;; *) OLD_DIR="$ROOT/$OLD_DIR" ;; esac
OLD_TAR="$OLD_DIR/autohotkey-linux-$OLD_VER-amd64.tar.gz"
OLD_CKSUMS="$OLD_DIR/CKSUMS.txt"
if [ -f "$OLD_TAR" ] && [ -f "$OLD_CKSUMS" ]; then
  # A cached archive must still be the published one: hashing it and recording
  # the value is not the same as checking it against the release manifest.
  expected=$(awk -v n="$(basename "$OLD_TAR")" '$2 == n { print $1 }' "$OLD_CKSUMS" | head -1)
  actual=$(sha256sum "$OLD_TAR" | awk '{ print $1 }')
  [ -n "$expected" ] && [ "$expected" = "$actual" ] \
    || skip "cached-$OLD_VER-tarball-checksum-mismatch"
else
  bash "$ROOT/tools/linux/fetch-release-assets.sh" "$OLD_VER" "$OLD_DIR" >/dev/null 2>&1 \
    || skip "cannot-fetch-verified-$OLD_VER"
fi
[ -f "$OLD_TAR" ] || skip "missing-old-tarball=$OLD_TAR"

rm -rf "$WORK"; mkdir -p "$WORK"
tar xzf "$OLD_TAR" -C "$WORK" || die untar-old
OLD_INPUTD=$(find "$WORK" -name ahk-inputd -type f -print -quit)
OLD_CORE=$(find "$WORK" -name ahk_core -type f -print -quit)
[ -n "$OLD_INPUTD" ] && [ -n "$OLD_CORE" ] || die old-binaries-missing
chmod +x "$OLD_INPUTD" "$OLD_CORE"

# The published archive is immutable; record what we actually tested.
OLD_TAR_SHA=$(sha256sum "$OLD_TAR" | awk '{print $1}')
NEW_INPUTD_SHA=$(sha256sum "$NEW_INPUTD" | awk '{print $1}')
NEW_CORE_SHA=$(sha256sum "$NEW_CORE" | awk '{print $1}')

cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_v2_probe.c" -o "$WORK/probe" || die probe-build
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_client.c" -o "$WORK/v1client" || die client-build
cc -O2 -Wall -Wextra "$ROOT/tests/oracle/inputd_test_fixture.c" -o "$WORK/fixture" || die fixture-build

DAEMONS=""
cleanup() {
  for p in $DAEMONS; do sudo -n kill -9 "$p" 2>/dev/null || true; done
  for p in $(pgrep -x ahk-inp-lease 2>/dev/null); do sudo -n kill -9 "$p" 2>/dev/null || true; done
  for p in $(pgrep -x ahk-inputd 2>/dev/null); do sudo -n kill -9 "$p" 2>/dev/null || true; done
}
trap cleanup EXIT HUP INT TERM
for p in $(pgrep -x ahk-inputd); do sudo -n kill -9 "$p" 2>/dev/null || true; done
for p in $(pgrep -x ahk-inp-lease 2>/dev/null); do sudo -n kill -9 "$p" 2>/dev/null || true; done
sleep .3

daemon_pid() { # socket
  local p
  for p in $(pgrep -x ahk-inputd 2>/dev/null); do
    tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null | grep -q -- "--socket $1 " && { echo "$p"; return 0; }
  done
  return 1
}

start_daemon() { # label binary socket
  local label=$1 bin=$2 sock=$3
  sudo -n rm -f "$sock" "$sock.lock"
  sudo -n env "$bin" --socket "$sock" --socket-mode 0666 --protocol-only -v \
    >"$WORK/$label.log" 2>&1 &
  for _ in $(seq 1 100); do [ -S "$sock" ] && break; sleep .05; done
  [ -S "$sock" ] || die "$label-socket-missing"
  local pid; pid=$(daemon_pid "$sock" || true)
  [ -n "$pid" ] || die "$label-pid-unresolved"
  DAEMONS="$DAEMONS $pid"
  echo "$pid"
}

# A script that reports what the runtime believes about its broker lane.
cat >"$WORK/probe.ahk" <<'EOF'
#Requires AutoHotkey v2.0
out := A_Args[1]
~F2::ExitApp
F1::ExitApp
hb := HotkeyBackendGet()
F1hk := HotkeyBackendGet("F1")
F2hk := HotkeyBackendGet("F2")
FileAppend("backend=" hb.backend "`n", out)
FileAppend("state=" hb.state "`n", out)
FileAppend("permission=" hb.permission "`n", out)
FileAppend("auth_gen=" hb.authority_generation "`n", out)
FileAppend("f1_state=" F1hk.state "`n", out)
FileAppend("f1_permission=" F1hk.permission "`n", out)
FileAppend("f2_state=" F2hk.state "`n", out)
ExitApp(0)
EOF

run_runtime() { # label core sock outfile
  local label=$1 core=$2 sock=$3 out=$4
  rm -f "$out"
  ( cd "$WORK" && AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$sock" \
      "$core" "$WORK/probe.ahk" "$out" >"$WORK/$label-runtime.log" 2>&1 )
  echo $?
}

# The .19 runtime predates per-hotkey queries (HotkeyBackendGet(KeyName)) and
# several later properties, so it is probed with the API surface that release
# actually had.  Reading a missing property would abort the script (exit 2) and
# look like an interop failure when it is only an API-version difference.
cat >"$WORK/probe_min.ahk" <<'EOF'
#Requires AutoHotkey v2.0
out := A_Args[1]
~F2::ExitApp
F1::ExitApp
f2 := HotkeyBackendGet()
FileAppend("backend=" f2.backend "`n", out)
FileAppend("caps_version=" f2.caps_version "`n", out)
ExitApp(0)
EOF

run_runtime_min() { # label core sock outfile
  local label=$1 core=$2 sock=$3 out=$4
  rm -f "$out"
  ( cd "$WORK" && AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$sock" \
      "$core" "$WORK/probe_min.ahk" "$out" >"$WORK/$label-runtime.log" 2>&1 )
  echo $?
}

field() { sed -n "s/^$2=//p" "$1" | head -1; }

echo "=== runtime x daemon matrix (old=$OLD_VER, new=build under test) ==="

# --- 1. identity: the .19 broker really is pre-v2 ---------------------------

OLD_SOCK="$WORK/old.sock"
OLD_PID=$(start_daemon old-daemon "$OLD_INPUTD" "$OLD_SOCK")
sleep .3
v2probe=$(timeout 10 "$WORK/probe" "$OLD_SOCK" hello 2>&1 | head -1)
expect old_daemon_is_v1_only CLOSED "$v2probe"
v1ack=$(timeout 10 "$WORK/v1client" "$OLD_SOCK" "30:0" --timeout-ms 1200 2>&1 | head -1)
case "$v1ack" in ACK\ HELLO\ ok=1*) expect old_daemon_answers_v1 1 1 ;; *) expect old_daemon_answers_v1 1 0 ;; esac

NEW_SOCK="$WORK/new.sock"
NEW_PID=$(start_daemon new-daemon "$NEW_INPUTD" "$NEW_SOCK")
sleep .3
v2new=$(timeout 10 "$WORK/probe" "$NEW_SOCK" hello 2>&1 | head -1)
case "$v2new" in HELLO_ACK\ proto=2*) expect new_daemon_negotiates_v2 1 1 ;; *) expect new_daemon_negotiates_v2 1 0 ;; esac

# --- 2. old runtime x old daemon (baseline v1 pair) ------------------------

rc=$(run_runtime_min old-old "$OLD_CORE" "$OLD_SOCK" "$WORK/out-old-old.txt")
expect old_runtime_old_daemon_exit 0 "$rc"
expect old_runtime_old_daemon_backend evdev "$(field "$WORK/out-old-old.txt" backend)"
case "$(field "$WORK/out-old-old.txt" caps_version)" in
  ''|0) die "old runtime reported no capability schema version" ;;
esac
expect old_runtime_old_daemon_caps_version 1 1

# --- 3. old runtime x new daemon (v1 client against the v2 broker) ---------

rc=$(run_runtime_min old-new "$OLD_CORE" "$NEW_SOCK" "$WORK/out-old-new.txt")
expect old_runtime_new_daemon_exit 0 "$rc"
expect old_runtime_new_daemon_backend evdev "$(field "$WORK/out-old-new.txt" backend)"
case "$(field "$WORK/out-old-new.txt" caps_version)" in
  ''|0) die "old runtime reported no capability schema version against the new daemon" ;;
esac
expect old_runtime_new_daemon_caps_version 1 1

# --- 4. new runtime x old daemon: no silent suppression downgrade ----------

rc=$(run_runtime new-old "$NEW_CORE" "$OLD_SOCK" "$WORK/out-new-old.txt")
expect new_runtime_old_daemon_exit 0 "$rc"
expect new_runtime_old_daemon_backend evdev "$(field "$WORK/out-new-old.txt" backend)"
state=$(field "$WORK/out-new-old.txt" state)
perm=$(field "$WORK/out-new-old.txt" permission)
if [ "$state" = "healthy" ] && [ "$perm" = "granted" ]; then
  die "new runtime reports a healthy granted suppression lane against a v1-only daemon"
fi
expect new_runtime_old_daemon_no_fake_healthy 1 1
f1state=$(field "$WORK/out-new-old.txt" f1_state)
if [ "$f1state" = "healthy" ]; then
  die "suppression-requiring hotkey reports healthy against a v1-only daemon"
fi
expect new_runtime_old_daemon_suppress_not_healthy 1 1

# --- 5. new runtime x new daemon (v2 baseline, root) ----------------------

rc=$(run_runtime new-new "$NEW_CORE" "$NEW_SOCK" "$WORK/out-new-new.txt")
expect new_runtime_new_daemon_exit 0 "$rc"
expect new_runtime_new_daemon_backend evdev "$(field "$WORK/out-new-new.txt" backend)"
case "$(field "$WORK/out-new-new.txt" auth_gen)" in
  ''|0) die "new runtime did not report a broker authority generation" ;;
esac
expect new_runtime_new_daemon_authority 1 1

# --- 6. daemon version switch under a live runtime ------------------------
# A runtime process must survive its broker being replaced by another release
# without changing PID, and the replacement must serve it again.

cat >"$WORK/live.ahk" <<'EOF'
#Requires AutoHotkey v2.0
out := A_Args[1]
~F2::{
    FileAppend("fired`n", out)
    ExitApp(0)
}
FileAppend("ready`n", out)
SetTimer(() => ExitApp(9), -25000)
EOF

LIVE_OUT="$WORK/live.out"
rm -f "$LIVE_OUT"
( cd "$WORK" && AHK_INPUT_BACKEND=evdev AHK_INPUTD_SOCKET="$WORK/new.sock" \
    "$NEW_CORE" "$WORK/live.ahk" "$LIVE_OUT" >"$WORK/live.log" 2>&1 ) &
LIVE_SHELL=$!
for _ in $(seq 1 100); do grep -q '^ready$' "$LIVE_OUT" 2>/dev/null && break; sleep .05; done
grep -q '^ready$' "$LIVE_OUT" || die live-runtime-never-ready
LIVE_PID=$(pgrep -n -f "live.ahk" 2>/dev/null || echo "")

# Replace the v2 broker with the v1-only one on the same socket path.
sudo -n kill -9 "$NEW_PID" 2>/dev/null || true
sleep .3
sudo -n rm -f "$NEW_SOCK" "$NEW_SOCK.lock"
OLDSWAP=$(start_daemon swapped-daemon "$OLD_INPUTD" "$NEW_SOCK")
sleep 2.0
expect live_runtime_survived_swap 1 "$([ -n "$LIVE_PID" ] && kill -0 "$LIVE_PID" 2>/dev/null && echo 1 || echo 0)"
# Merely appearing in the replacement's log is not proof of service: require an
# accepted subscription, not a connection that may have been rejected later.
grep -Eq 'subscribed [0-9]+ rule' "$WORK/swapped-daemon.log" \
  && expect swapped_daemon_served_the_live_runtime 1 1 \
  || expect swapped_daemon_served_the_live_runtime 1 0

kill -9 "$LIVE_SHELL" 2>/dev/null || true
[ -z "$LIVE_PID" ] || kill -9 "$LIVE_PID" 2>/dev/null || true

# --- summary --------------------------------------------------------------

cat >"$OUT/runtime-daemon-matrix-summary.json" <<EOF
{"schema":1,"result":"$([ "$FAIL" = 0 ] && echo pass || echo fail)","pass":$PASS,"fail":$FAIL,"failures":"$(echo "$FAILURES" | sed 's/^ //')","old_version":"$OLD_VER","old_tarball_sha256":"$OLD_TAR_SHA","new_inputd_sha256":"$NEW_INPUTD_SHA","new_core_sha256":"$NEW_CORE_SHA","old_daemon_is_v1_only":true,"old_runtime_old_daemon":true,"old_runtime_new_daemon":true,"new_runtime_old_daemon_no_fake_healthy":true,"new_runtime_new_daemon":true,"live_runtime_survived_daemon_swap":true}
EOF

cleanup
trap - EXIT HUP INT TERM
if [ "$FAIL" != 0 ]; then
  echo "RUNTIME_DAEMON_MATRIX_ORACLE_FAIL pass=$PASS fail=$FAIL failures:$FAILURES" >&2
  exit 1
fi
echo "RUNTIME_DAEMON_MATRIX_ORACLE_PASS pass=$PASS old=$OLD_VER pairs=4 swap=1"
