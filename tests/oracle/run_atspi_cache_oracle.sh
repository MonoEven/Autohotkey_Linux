#!/bin/bash
# M5-C: prove AT-SPI Cache.GetItems bulk discovery and the compatibility
# GetChildren fallback against an independent GTK3 process. Requires a GNOME
# Wayland session with accessibility enabled.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
CC="${CC:-cc}"
$CC -O2 -Wall -Wextra -o "$OUT/gtk-ok" "$ROOT/tests/oracle/gtk_ok.c" \
  $(pkg-config --cflags --libs gtk+-3.0) || exit 2

export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY

cat >/tmp/m5cache.ahk <<'EOF'
#Requires AutoHotkey v2.0
text := ControlGetText("CACHE-ENTRY", "CacheWin")
FileAppend("text=" text "`n", "/tmp/m5cache.out")
ExitApp
EOF

run_case() {
  mode="$1"
  pkill -f 'gtk-ok --title CacheWin' 2>/dev/null
  rm -f /tmp/m5cache.out "/tmp/m5cache_${mode}.dump" "/tmp/m5cache_${mode}.log"
  "$OUT/gtk-ok" --title CacheWin --name CACHE-ENTRY --text CacheText \
    >/tmp/m5cache_gtk.log 2>&1 &
  GPID=$!
  sleep 4
  if [ "$mode" = fallback ]; then
    AHK_ATSPI_DISABLE_CACHE=1 AHK_ATSPI_DUMP="/tmp/m5cache_${mode}.dump" \
      timeout -k 2 120 "$BIN" /tmp/m5cache.ahk >"/tmp/m5cache_${mode}.log" 2>&1
  else
    AHK_ATSPI_DUMP="/tmp/m5cache_${mode}.dump" \
      timeout -k 2 120 "$BIN" /tmp/m5cache.ahk >"/tmp/m5cache_${mode}.log" 2>&1
  fi
  rc=$?
  kill "$GPID" 2>/dev/null; wait "$GPID" 2>/dev/null
  [ "$rc" = 0 ] || { echo "ATSPI_${mode}_RC_FAIL rc=$rc"; cat "/tmp/m5cache_${mode}.log"; exit 1; }
  grep -q '^text=CacheText$' /tmp/m5cache.out || { echo "ATSPI_${mode}_TEXT_FAIL"; cat /tmp/m5cache.out; exit 1; }
}

run_case cache
cache_header="$(head -1 /tmp/m5cache_cache.dump 2>/dev/null)"
cache_apps="$(printf '%s' "$cache_header" | sed -n 's/.*cache_apps=\([0-9][0-9]*\).*/\1/p')"
cache_items="$(printf '%s' "$cache_header" | sed -n 's/.*cache_items=\([0-9][0-9]*\).*/\1/p')"
[ -n "$cache_apps" ] && [ "$cache_apps" -gt 0 ] \
  || { echo "ATSPI_CACHE_NOT_USED header=[$cache_header]"; exit 1; }
[ -n "$cache_items" ] && [ "$cache_items" -gt 0 ] \
  || { echo "ATSPI_CACHE_EMPTY header=[$cache_header]"; exit 1; }

run_case fallback
fallback_header="$(head -1 /tmp/m5cache_fallback.dump 2>/dev/null)"
fallback_cache="$(printf '%s' "$fallback_header" | sed -n 's/.*cache_apps=\([0-9][0-9]*\).*/\1/p')"
fallback_apps="$(printf '%s' "$fallback_header" | sed -n 's/.*fallback_apps=\([0-9][0-9]*\).*/\1/p')"
[ "$fallback_cache" = 0 ] && [ -n "$fallback_apps" ] && [ "$fallback_apps" -gt 0 ] \
  || { echo "ATSPI_FALLBACK_NOT_USED header=[$fallback_header]"; exit 1; }

pkill -f 'gtk-ok --title CacheWin' 2>/dev/null
cat >"$OUT/atspi-cache-summary.json" <<EOF
{"schema":1,"result":"pass","cache_apps":$cache_apps,"cache_items":$cache_items,"fallback_apps":$fallback_apps,"text":"CacheText"}
EOF
echo "ATSPI_CACHE_ORACLE_PASS cache_apps=$cache_apps cache_items=$cache_items fallback_apps=$fallback_apps"
