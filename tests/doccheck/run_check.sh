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

# D-Bus session bus for the COM (D-Bus) assertions.  Start a private
# session daemon and export its address so ahk_core (and any child) can
# connect; ComObject/ComObjGet proxies then work against a real bus.
DBUS_PID=""
if command -v dbus-daemon >/dev/null 2>&1; then
  DBUS_ADDR="unix:path=/tmp/ahk_dc_bus"
  rm -f /tmp/ahk_dc_bus
  dbus-daemon --session --fork --address="$DBUS_ADDR" 2>/dev/null
  DBUS_PID=$(pgrep -f "dbus-daemon.*ahk_dc_bus" | head -1)
  export DBUS_SESSION_BUS_ADDRESS="$DBUS_ADDR"
  echo "D-Bus session bus at $DBUS_ADDR (pid ${DBUS_PID:-unknown})"
fi

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
    Xvfb :99 -screen 0 1024x768x24 > out/xvfb.log 2>&1 &
    XVFB_PID=$!
    # Wait for the X socket to be connectable (up to 5 s) instead of a
    # fixed sleep; on loaded CI runners Xvfb can take a moment to start.
    for _ in 1 2 3 4 5 6 7 8 9 10; do
      if DISPLAY=:99 timeout 2 xdpyinfo >/dev/null 2>&1; then
        break
      fi
      if ! kill -0 "$XVFB_PID" 2>/dev/null; then
        echo "ERROR: Xvfb :99 exited during startup" >&2
        break
      fi
      sleep 0.5
    done
    gcc -o out/xwin_helper xwin_helper.c -lX11 2>/dev/null
    gcc -o out/xkeycap xkeycap.c -lX11 2>/dev/null
    gcc -o out/xshape_probe xshape_probe.c -lX11 -lXext 2>/dev/null
    # Editor marker for the assert_edit Edit() check: records its arguments.
    printf '#!/bin/sh\necho "$@" > /tmp/ahk_dc_edit_marker.txt\n' > /tmp/ahk_edit_marker.sh
    chmod +x /tmp/ahk_edit_marker.sh
  fi
fi
trap 'kill $HTTP_PID 2>/dev/null; [ -n "$XVFB_PID" ] && kill $XVFB_PID 2>/dev/null; [ -n "$DBUS_PID" ] && kill $DBUS_PID 2>/dev/null' EXIT

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
  if [ "$base" = "assert_hotkey_pt" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_hotkey_pt (run with --xvfb: xkeycap foreground client)"
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
  if [ "$base" = "assert_gui" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_gui (run with --xvfb)"
    continue
  fi
  if [ "$base" = "assert_misc_cov" ] && [ "$XVFB" != 1 ]; then
    echo "SKIP: assert_misc_cov (run with --xvfb: xwin_helper windows + D-Bus)"
    continue
  fi
  if [ "$base" = "assert_wayland" ]; then
    echo "SKIP: assert_wayland (run with wayland_run.sh -- sway headless)"
    continue
  fi
  # Some suites need script arguments (e.g. assert_general checks A_Args);
  # run those with args instead of the plain invocation.
  case "$base" in
    assert_general) extra=("one" "two") ;;
    *) extra=() ;;
  esac
  # Display-dependent suites run under Xvfb (MsgBox would open a real
  # dialog with a display present); everything else stays headless.
  case "$base" in
    assert_win|assert_input|assert_ctrl|assert_monitor|assert_timer|assert_hotkey|assert_hotkey_pt|assert_edit|assert_dialog|assert_msg|assert_image|assert_shape|assert_gui|assert_statements|assert_misc_cov)
      XDISPLAY=:99 ;;
    *) XDISPLAY="" ;;
  esac
  # The runner's stdout/stderr (and anything a child inherits, e.g.
  # xwin_helper's Xlib error output) goes to a *separate* raw log; the
  # final assertion file is published from the script's own output file
  # via tmp+rename so no late writer holding the old inode can corrupt it.
  raw="out/${base}.proc.log"
  final="out/${base}.txt"
  tmp="out/${base}.txt.tmp"
  rm -f "$raw" "$final" "$tmp"
  # The GUI suite intentionally exercises GTK3, which keeps process-lifetime
  # caches (fontconfig/pango/GTK internals) that LeakSanitizer flags at exit
  # even for a healthy app.  ASan memory-safety checks stay enabled; only the
  # LSan leak report (which makes the runner exit non-zero) is suppressed.
  if [ "$base" = "assert_gui" ]; then
    export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0"
  fi
  # The interpreter installs a SIGTERM handler (Reload protocol), so a
  # plain `timeout` SIGTERM alone may not terminate a stuck script; -k 5
  # escalates to SIGKILL as a hard safety net.
  DISPLAY=$XDISPLAY timeout -k 5 60 "$BIN" "$ahk" "${extra[@]}" > "$raw" 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then
    fail=$((fail+1))
    echo "FAIL: $base (runner exit=$rc)"
    echo "DIAG $base raw tail: $(tail -3 "$raw" 2>/dev/null | tr '\n' ';')"
    continue
  fi
  # Publish the suite's own output file (if any) as the assertion source.
  out_src=""
  case "$base" in
    assert_win)     out_src="/tmp/ahk_dc_win_out.txt" ;;
    assert_input)   out_src="/tmp/ahk_dc_input_out.txt" ;;
    assert_ctrl)    out_src="/tmp/ahk_dc_ctrl_out.txt" ;;
    assert_monitor) out_src="/tmp/ahk_dc_monitor_out.txt" ;;
    assert_timer)   out_src="/tmp/ahk_dc_timer_out.txt" ;;
    assert_hotkey)  out_src="/tmp/ahk_dc_hotkey_out.txt" ;;
    assert_hotkey_pt) out_src="/tmp/ahk_dc_hotkeypt_out.txt" ;;
    assert_edit)    out_src="/tmp/ahk_dc_edit_out.txt" ;;
    assert_dialog)  out_src="/tmp/ahk_dc_dialog_out.txt" ;;
    assert_msg)     out_src="/tmp/ahk_dc_msg_out.txt" ;;
    assert_image)   out_src="/tmp/ahk_dc_image_out.txt" ;;
    assert_shape)   out_src="/tmp/ahk_dc_shape_out.txt" ;;
    assert_gui)     out_src="/tmp/ahk_dc_gui_out.txt" ;;
    assert_notimpl) out_src="/tmp/ahk_dc_notimpl_out.txt" ;;
    assert_sound_etc) out_src="/tmp/ahk_dc_soundetc_out.txt" ;;
    assert_statements) out_src="/tmp/ahk_dc_statements_out.txt" ;;
    assert_misc_cov) out_src="/tmp/ahk_dc_misc_out.txt" ;;
  esac
  if [ -n "$out_src" ] && [ -f "$out_src" ]; then
    cp "$out_src" "$tmp" && mv -f "$tmp" "$final"
    # Diagnostics: is a late writer still growing the raw log after the
    # script returned?  (Proves an inherited-fd descendant keeps writing.)
    size1=$(stat -c %s "$raw" 2>/dev/null || echo 0)
    sleep 1
    size2=$(stat -c %s "$raw" 2>/dev/null || echo 0)
    if [ "$size1" != "$size2" ]; then
      echo "DIAG $base raw_log_grew_after_exit: $size1 -> $size2 (late writer!)"
      ps -ef | grep -E '[x]win_helper|[X]vfb' | head -5
    fi
  else
    # No separate output file: the raw log (stdout) is the assertion source.
    mv -f "$raw" "$final"
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
      # Diagnostics for flaky failures: show the raw lines around the
      # assertion and the tail of the output file.  Prefix with "DIAG" so
      # GitHub's log folding cannot swallow the lines.
      if [ "$fail" -le 12 ]; then
        echo "DIAG base=$base name=$name out=out/${base}.txt"
        echo "DIAG raw-grep: [$(grep -n "^${name}=" "out/${base}.txt" | head -3 | tr '\n' ';')]"
        echo "DIAG tail: [$(tail -4 "out/${base}.txt" | tr '\n' ';')]"
        echo "DIAG expect: [$(grep -n "^${name}=" "assert_${base}_expect.txt" | tr '\n' ';')]"
        echo "DIAG wc: $(wc -c < "out/${base}.txt" 2>/dev/null || echo missing)"
      fi
    fi
  done < "$exp"
done

echo "=============================="
echo "DOC-CHECK PASS=$pass FAIL=$fail"
[ "$fail" = 0 ]
