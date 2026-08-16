#!/bin/bash
# Doc-check assertion runner.
# Each assert_*.ahk prints "name=value" lines (headless MsgBox).
# Each assert_*_expect.txt contains "name=value" expected lines (from the v2 docs).
# Usage: run_check.sh [--xvfb] [path-to-ahk_core]
#   --xvfb: run the display-dependent suite (assert_win) under Xvfb :99 with
#           the xwin_helper test client; other suites stay headless.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$SCRIPT_DIR" || exit 1

XVFB=0
if [ "$1" = "--xvfb" ]; then
  XVFB=1
  shift
fi
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
fi

# Display-dependent suite support (assert_win/assert_input): Xvfb + helper clients.
XVFB_PID=""
if [ "$XVFB" = 1 ]; then
  if ! command -v Xvfb >/dev/null 2>&1 || ! command -v gcc >/dev/null 2>&1; then
    echo "SKIP: assert_win/assert_input (Xvfb or gcc not installed)"
  else
    pkill -f "Xvfb :99" 2>/dev/null
    rm -f /tmp/.X99-lock
    sleep 0.3
    Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &
    XVFB_PID=$!
    sleep 1
    gcc -o out/xwin_helper xwin_helper.c -lX11 2>/dev/null
    gcc -o out/xkeycap xkeycap.c -lX11 2>/dev/null
    gcc -o out/xshape_probe xshape_probe.c -lX11 -lXext 2>/dev/null
    # Editor marker for the assert_edit Edit() check: records its arguments.
    printf '#!/bin/sh\necho "$@" > /tmp/ahk_dc_edit_marker.txt\n' > /tmp/ahk_edit_marker.sh
    chmod +x /tmp/ahk_edit_marker.sh
  fi
fi
trap 'kill $HTTP_PID 2>/dev/null; [ -n "$XVFB_PID" ] && kill $XVFB_PID 2>/dev/null' EXIT

pass=0; fail=0
for ahk in assert_*.ahk; do
  base="${ahk%.ahk}"
  exp="${base}_expect.txt"
  if [ ! -f "$exp" ]; then
    echo "SKIP: $base (no expect file)"
    continue
  fi
  if [ "$base" = "assert_win" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_win (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_input" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_input (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_ctrl" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_ctrl (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_monitor" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_monitor (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_timer" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_timer (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_hotkey" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_hotkey (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_edit" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_edit (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_dialog" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_dialog (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_msg" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_msg (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_image" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_image (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_shape" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_shape (run with --xvfb)"
    continue
  fi
  # Some suites need script arguments (e.g. assert_general checks A_Args);
  # run those with args instead of the plain invocation.
  case "$base" in
    assert_general) extra=("one" "two") ;;
    *) extra=() ;;
  esac
  # assert_win/assert_input run under Xvfb and write their output to a file
  # (MsgBox would open a real dialog with a display present).
  if [ "$base" = "assert_win" ] || [ "$base" = "assert_input" ] || [ "$base" = "assert_ctrl" ] || [ "$base" = "assert_monitor" ] || [ "$base" = "assert_timer" ] || [ "$base" = "assert_hotkey" ] || [ "$base" = "assert_edit" ] || [ "$base" = "assert_dialog" ] || [ "$base" = "assert_msg" ] || [ "$base" = "assert_image" ] || [ "$base" = "assert_shape" ]; then
    XDISPLAY=:99
  else
    XDISPLAY=""
  fi
  DISPLAY=$XDISPLAY timeout 60 "$BIN" "$ahk" "${extra[@]}" > "out/${base}.txt" 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then
    fail=$((fail+1))
    echo "FAIL: $base (runner exit=$rc)"
    continue
  fi
  if [ "$base" = "assert_win" ]; then
    cp /tmp/ahk_dc_win_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_input" ]; then
    cp /tmp/ahk_dc_input_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_ctrl" ]; then
    cp /tmp/ahk_dc_ctrl_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_monitor" ]; then
    cp /tmp/ahk_dc_monitor_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_timer" ]; then
    cp /tmp/ahk_dc_timer_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_hotkey" ]; then
    cp /tmp/ahk_dc_hotkey_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_edit" ]; then
    cp /tmp/ahk_dc_edit_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_dialog" ]; then
    cp /tmp/ahk_dc_dialog_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_msg" ]; then
    cp /tmp/ahk_dc_msg_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_image" ]; then
    cp /tmp/ahk_dc_image_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  if [ "$base" = "assert_shape" ]; then
    cp /tmp/ahk_dc_shape_out.txt "out/${base}.txt" 2>/dev/null || true
  fi
  # assert_display: the ListVars/ListHotkeys/KeyHistory dumps go to stdout;
  # check the freeform content patterns from assert_display_content.txt.
  if [ "$base" = "assert_display" ] && [ -f "assert_display_content.txt" ]; then
    while IFS= read -r pat; do
      [ -z "$pat" ] && continue
      if grep -qF -- "$pat" "out/${base}.txt"; then
        pass=$((pass+1))
      else
        fail=$((fail+1))
        echo "FAIL: $base/content missing [$pat]"
      fi
    done < "assert_display_content.txt"
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
