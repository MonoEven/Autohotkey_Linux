#!/bin/bash
# Cross-version (N-1 <-> N) upgrade/downgrade acceptance oracle (Audit47 I11).
#
# tools/linux/verify-packages.sh proves that a release reinstalls ITSELF over
# the same prefix.  That is not the contract users hit: they install one
# release and later move to another, in either direction.  This oracle proves
# the real N-1 -> N -> N-1 -> uninstall -> N path against the actual published
# tarballs, and that a script which is already running survives the upgrade
# (the interpreter is replaced on disk while the running process keeps using
# the old inode).
#
# Usage: run_upgrade_matrix_oracle.sh [PREV_ASSET_DIR] [CURR_ASSET_DIR]
#   PREV_ASSET_DIR  directory containing autohotkey-linux-<N-1>-amd64.tar.gz
#                   (default: $AHK_MATRIX_PREV_DIR, else the highest
#                    dist/release-linux.* directory that is not <N>)
#   CURR_ASSET_DIR  directory containing autohotkey-linux-<N>-amd64.tar.gz
#                   (default: $AHK_MATRIX_CURR_DIR, else <repo>/dist)
# Exit codes: 0 pass, 1 assertion/environment failure, 2 required assets absent.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT" || exit 1

PFX=/tmp/ahk-upgrade-matrix-prefix
WORK=/tmp/ahk-upgrade-matrix
LONGRUN_PID=""

cleanup() {
  if [ -n "$LONGRUN_PID" ]; then
    kill -9 "$LONGRUN_PID" 2>/dev/null || true
    LONGRUN_PID=""
  fi
  rm -rf "$PFX" "$WORK"
}

die() { # <step> <detail>
  echo "UPGRADE_MATRIX_FAIL step=$1 detail=$2" >&2
  cleanup
  exit 1
}
skip() { # <detail>
  echo "UPGRADE_MATRIX_SKIP missing-assets=$1"
  cleanup
  exit 2
}

tar_for() { # <dir> -> highest-versioned tarball path (empty when none)
  ls "$1"/autohotkey-linux-*-amd64.tar.gz 2>/dev/null | sort -V | tail -1
}

ver_of() { # <tarball path> -> version
  basename "$1" | sed -n 's/^autohotkey-linux-\(.*\)-amd64\.tar\.gz$/\1/p'
}

abs_dir() { # <path> -> absolute
  case "$1" in
    /*) printf '%s' "$1" ;;
    *)  printf '%s/%s' "$ROOT" "$1" ;;
  esac
}

# --- resolve the two release versions and their asset directories -----------

CURR_VER=$(tr -d '\r\n' < "$ROOT/tools/linux/VERSION" 2>/dev/null)
[ -n "$CURR_VER" ] || die resolve "tools/linux/VERSION is missing or empty"

CURR_DIR=$(abs_dir "${2:-${AHK_MATRIX_CURR_DIR:-dist}}")
CURR_TAR="$CURR_DIR/autohotkey-linux-$CURR_VER-amd64.tar.gz"
[ -f "$CURR_TAR" ] || skip "$CURR_TAR"

prev_arg="${1:-${AHK_MATRIX_PREV_DIR:-}}"
if [ -n "$prev_arg" ]; then
  PREV_DIR=$(abs_dir "$prev_arg")
  PREV_TAR=$(tar_for "$PREV_DIR")
  [ -n "$PREV_TAR" ] || skip "$PREV_DIR/autohotkey-linux-*-amd64.tar.gz"
else
  # No explicit previous directory: take the highest local release-linux.*
  # directory whose tarball is NOT the version being built.
  PREV_TAR=""
  for d in "$ROOT"/dist/release-linux.*; do
    [ -d "$d" ] || continue
    t=$(tar_for "$d")
    [ -n "$t" ] || continue
    v=$(ver_of "$t")
    [ -n "$v" ] || continue
    [ "$v" != "$CURR_VER" ] || continue
    if [ -z "$PREV_TAR" ]; then
      PREV_TAR="$t"
    else
      cur=$(ver_of "$PREV_TAR")
      highest=$(printf '%s\n%s\n' "$cur" "$v" | sort -V | tail -1)
      [ "$highest" = "$v" ] && PREV_TAR="$t"
    fi
  done
  if [ -z "$PREV_TAR" ]; then
    echo "UPGRADE_MATRIX_SKIP missing-assets=no previous release under $ROOT/dist (pass PREV_ASSET_DIR or set AHK_MATRIX_PREV_DIR)"
    cleanup
    exit 2
  fi
  PREV_DIR=$(dirname "$PREV_TAR")
fi

PREV_VER=$(ver_of "$PREV_TAR")
[ -n "$PREV_VER" ] || die resolve "cannot parse a version from $PREV_TAR"
[ "$PREV_VER" != "$CURR_VER" ] \
  || die resolve "previous and current versions are both $CURR_VER; this is not a cross-version matrix"

echo "=== Cross-version upgrade matrix: N-1=$PREV_VER (from $PREV_DIR) -> N=$CURR_VER (from $CURR_DIR) ==="

# --- helpers ---------------------------------------------------------------

install_from() { # <tarball> <prefix>
  rm -rf "$WORK/extract"
  mkdir -p "$WORK/extract" || return 1
  tar xzf "$1" -C "$WORK/extract" || return 1
  inst=$(find "$WORK/extract" -path '*/tools/linux/install.sh' -print -quit)
  [ -n "$inst" ] || return 1
  ( cd "$(dirname "$inst")" && ./install.sh --prefix "$2" --yes ) \
    >"$WORK/install.log" 2>&1
}

launcher_version() { # -> stamped release, empty on failure
  "$PFX/bin/ahk" --version 2>/dev/null \
    | sed -n 's/.*Linux port v\(.*\)$/\1/p' | head -1
}

assert_version() { # <step> <expected>
  got=$(launcher_version)
  [ "$got" = "$2" ] || die "$1" "launcher version is '$got', expected '$2'"
}

assert_smoke() { # <step> <label>
  out="$WORK/smoke-$2.txt"
  rm -f "$out"
  cat >"$WORK/smoke.ahk" <<'EOF'
#Requires AutoHotkey v2.0
FileAppend("smoke-ok`n", A_Args[1])
ExitApp(0)
EOF
  "$PFX/bin/ahk" "$WORK/smoke.ahk" "$out" >"$WORK/smoke-$2.log" 2>&1 \
    || die "$1" "interpreter failed to run a script (see $WORK/smoke-$2.log)"
  grep -q '^smoke-ok$' "$out" \
    || die "$1" "script produced no expected output in $out"
}

assert_hygiene() { # <step> <expected release>
  n_core=$(find "$PFX" -name ahk_core -type f | wc -l | tr -d ' ')
  n_inputd=$(find "$PFX" -name ahk-inputd -type f | wc -l | tr -d ' ')
  n_pack=$(find "$PFX" -name ahk_core_pack -type f | wc -l | tr -d ' ')
  [ "$n_core" = "1" ] || die "$1" "prefix holds $n_core ahk_core files, expected 1"
  [ "$n_inputd" = "1" ] || die "$1" "prefix holds $n_inputd ahk-inputd files, expected 1"
  [ "$n_pack" = "1" ] || die "$1" "prefix holds $n_pack ahk_core_pack files, expected 1"
  stamped=$(sed -n 's/^AHK_VERSION=//p' "$PFX/bin/ahk" | head -1 | tr -d '"')
  [ "$stamped" = "$2" ] \
    || die "$1" "launcher stamps AHK_VERSION='$stamped', expected '$2'"
  [ ! -e "$PFX/bin/ahk.new" ] \
    || die "$1" "update left a $PFX/bin/ahk.new staging file behind"
}

update_to() { # <step> <asset dir> <version>
  ( cd /tmp && AHK_RELEASE_DIR="$2" "$PFX/bin/ahk" --update "$3" ) \
    >"$WORK/update-$3.log" 2>&1
  rc=$?
  [ "$rc" = 0 ] || { cat "$WORK/update-$3.log" >&2; die "$1" "--update $3 exited $rc"; }
  grep -q "AutoHotkey updated to v$3" "$WORK/update-$3.log" \
    || { cat "$WORK/update-$3.log" >&2; die "$1" "update log does not report v$3"; }
}

# --- 1. install N-1 ---------------------------------------------------------

rm -rf "$PFX" "$WORK"
mkdir -p "$PFX" "$WORK" || die 1 "cannot create $PFX/$WORK"

install_from "$PREV_TAR" "$PFX" \
  || { cat "$WORK/install.log" >&2; die 1 "installing N-1 ($PREV_VER) failed"; }
[ -x "$PFX/bin/ahk" ] || die 1 "launcher missing after installing N-1"
assert_version 1 "$PREV_VER"
assert_smoke 1 "prev"
assert_hygiene 1 "$PREV_VER"

# --- 2. upgrade to N while a script is already running ----------------------

cat >"$WORK/longrun.ahk" <<'EOF'
#Requires AutoHotkey v2.0
Loop {
    FileAppend(A_TickCount "`n", A_Args[1])
    Sleep(200)
}
EOF
TICKS="$WORK/ticks.txt"
rm -f "$TICKS"
"$PFX/bin/ahk" "$WORK/longrun.ahk" "$TICKS" >"$WORK/longrun.log" 2>&1 &
LONGRUN_PID=$!
for _ in $(seq 1 50); do
  [ -s "$TICKS" ] && break
  sleep .1
done
[ -s "$TICKS" ] || die 2 "the long-running script never wrote a tick (pid $LONGRUN_PID)"
before_lines=$(wc -l < "$TICKS" | tr -d ' ')
kill -0 "$LONGRUN_PID" 2>/dev/null || die 2 "long-running script died before the upgrade"

update_to 2 "$CURR_DIR" "$CURR_VER"

kill -0 "$LONGRUN_PID" 2>/dev/null \
  || die 2 "the running script (pid $LONGRUN_PID) did not survive the upgrade"
after_lines=$before_lines
for _ in $(seq 1 50); do
  after_lines=$(wc -l < "$TICKS" | tr -d ' ')
  [ "$after_lines" -gt "$before_lines" ] && break
  sleep .1
done
[ "$after_lines" -gt "$before_lines" ] \
  || die 2 "the running script stopped ticking after the upgrade ($before_lines -> $after_lines)"

assert_version 2 "$CURR_VER"
assert_smoke 2 "curr"
assert_hygiene 2 "$CURR_VER"

kill -9 "$LONGRUN_PID" 2>/dev/null || true
LONGRUN_PID=""

# --- 3. downgrade back to N-1 ----------------------------------------------

update_to 3 "$PREV_DIR" "$PREV_VER"
assert_version 3 "$PREV_VER"
assert_smoke 3 "rollback"
assert_hygiene 3 "$PREV_VER"

# --- 4. uninstall, then a fresh install of N -------------------------------

"$PFX/bin/ahk" --uninstall >"$WORK/uninstall.log" 2>&1 \
  || { cat "$WORK/uninstall.log" >&2; die 4 "--uninstall exited non-zero"; }
[ ! -e "$PFX/bin/ahk" ] || die 4 "uninstall left the launcher in place"
[ ! -e "$PFX/share/autohotkey" ] || die 4 "uninstall left the library directory in place"

install_from "$CURR_TAR" "$PFX" \
  || { cat "$WORK/install.log" >&2; die 4 "fresh install of N ($CURR_VER) failed"; }
assert_version 4 "$CURR_VER"
assert_smoke 4 "fresh"
assert_hygiene 4 "$CURR_VER"

# --- summary ---------------------------------------------------------------

cat >"$OUT/upgrade-matrix-summary.json" <<EOF
{"schema":1,"result":"pass","previous_version":"$PREV_VER","current_version":"$CURR_VER","previous_asset_dir":"$PREV_DIR","current_asset_dir":"$CURR_DIR","install_prev":true,"upgrade_to_current":true,"running_script_survived":true,"ticks_before_upgrade":$before_lines,"ticks_after_upgrade":$after_lines,"downgrade_to_prev":true,"prefix_hygiene_after_upgrade":true,"prefix_hygiene_after_downgrade":true,"uninstall_removed_launcher":true,"uninstall_removed_library":true,"fresh_install_current":true}
EOF

cleanup
echo "UPGRADE_MATRIX_ORACLE_PASS prev=$PREV_VER curr=$CURR_VER upgrade=1 downgrade=1 running_script_survived=1 uninstall=1 reinstall=1"
