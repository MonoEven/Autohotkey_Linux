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

# assert_sys downloads from a local HTTP server (Download assertions).
HTTP_PORT=18765
HTTP_DIR=/tmp/ahk_dc_http
if command -v python3 >/dev/null 2>&1; then
  mkdir -p "$HTTP_DIR"
  printf 'AHK_DC_DOWNLOAD' > "$HTTP_DIR/serve.txt"
  python3 -m http.server "$HTTP_PORT" --directory "$HTTP_DIR" >/dev/null 2>&1 &
  HTTP_PID=$!
  sleep 0.5
  trap 'kill $HTTP_PID 2>/dev/null' EXIT
fi

pass=0; fail=0
for ahk in assert_*.ahk; do
  base="${ahk%.ahk}"
  exp="${base}_expect.txt"
  if [ ! -f "$exp" ]; then
    echo "SKIP: $base (no expect file)"
    continue
  fi
  # Some suites need script arguments (e.g. assert_general checks A_Args);
  # run those with args instead of the plain invocation.
  case "$base" in
    assert_general) extra=("one" "two") ;;
    *) extra=() ;;
  esac
  DISPLAY= timeout 60 "$BIN" "$ahk" "${extra[@]}" > "out/${base}.txt" 2>&1
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
