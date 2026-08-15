#!/bin/bash
# Doc-check assertion runner.
# Each assert_*.ahk prints "name=value" lines (headless MsgBox).
# Each assert_*_expect.txt contains "name=value" expected lines (from the v2 docs).
# Usage: run_check.sh [path-to-ahk_core]
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$SCRIPT_DIR" || exit 1
BIN=${1:-build-core/source/linux/core/ahk_core}
case "$BIN" in
  /*) ;;
  *) BIN="$REPO_DIR/$BIN" ;;
esac
mkdir -p out

pass=0; fail=0
for ahk in assert_*.ahk; do
  base="${ahk%.ahk}"
  exp="${base}_expect.txt"
  if [ ! -f "$exp" ]; then
    echo "SKIP: $base (no expect file)"
    continue
  fi
  DISPLAY= timeout 60 "$BIN" "$ahk" > "out/${base}.txt" 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then
    fail=$((fail+1))
    echo "FAIL: $base (runner exit=$rc)"
    continue
  fi
  while IFS= read -r line; do
    [ -z "$line" ] && continue
    name="${line%%=*}"
    want="${line#*=}"
    got="$(grep -E "^${name}=" "out/${base}.txt" | head -1 | cut -d= -f2-)"
    if [ "$got" = "$want" ]; then
      pass=$((pass+1))
    else
      fail=$((fail+1))
      echo "FAIL: $base/$name want=[$want] got=[$got]"
    fi
  done < "$exp"
done

echo "=============================="
echo "DOC-CHECK PASS=$pass FAIL=$fail"
[ "$fail" = 0 ]
