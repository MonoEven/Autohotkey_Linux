#!/bin/bash
# M6 real-host matrix: LibreOffice Calc 25.x through GTK3/AT-SPI on GNOME XWayland.
# Proves dialog Action and title-scoped bulk cache; Table cells remain explicit.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
command -v libreoffice >/dev/null || { echo LIBREOFFICE_ATSPI_SKIP libreoffice-missing; exit 2; }
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
auth=$(ls -1t "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
display=$(ps -u "$(id -u)" -o args= | sed -n 's/.*Xwayland \(:[0-9][0-9]*\).*/\1/p' | head -1)
[ -n "$auth" ] && [ -n "$display" ] \
  || { echo LIBREOFFICE_ATSPI_SKIP gnome-xwayland-session-missing; exit 2; }
PROFILE="/tmp/ahk-lo-profile-$$"
CSV="/tmp/AHK-Calc-Probe-$$.csv"
TITLE_BASE="AHK-Calc-Probe-$$.csv"
rm -rf "$PROFILE"
printf 'Calc-世界,Bravo\nAlpha,64\n' >"$CSV"
rm -f /tmp/lo_atspi_{result,dump,host.log,ahk.log}
LOPID=0
cleanup() {
  [ "$LOPID" = 0 ] || kill "$LOPID" 2>/dev/null || true
  [ "$LOPID" = 0 ] || wait "$LOPID" 2>/dev/null || true
  rm -rf "$PROFILE" "$CSV"
}
trap cleanup EXIT HUP INT TERM
DISPLAY="$display" XAUTHORITY="$auth" SAL_USE_VCLPLUGIN=gtk3 \
  SAL_ACCESSIBILITY_ENABLED=1 libreoffice --calc --norestore --nodefault \
  --nofirststartwizard -env:UserInstallation="file://$PROFILE" "$CSV" \
  >/tmp/lo_atspi_host.log 2>&1 &
LOPID=$!
sleep 7
kill -0 "$LOPID" 2>/dev/null \
  || { echo LIBREOFFICE_ATSPI_HOST_START_FAIL; cat /tmp/lo_atspi_host.log; exit 1; }

cat >/tmp/lo_atspi.ahk <<'EOF'
#Requires AutoHotkey v2.0
base := A_Args[1]
importTitle := "Text Import - [" base "]"
mainTitle := base " — LibreOffice Calc"
clickOk := 1
clickCode := 0
try ControlClick("OK", importTitle)
catch as caught
{
    clickOk := 0
    clickCode := A_LastError
}
Sleep(2500)
; Force a final title-scoped refresh/dump without claiming cell controls.
probe := ControlGetText("AHK-NOT-A-CONTROL", mainTitle)
FileAppend("click=" clickOk ":" clickCode "`nprobe=" probe "`n", "/tmp/lo_atspi_result")
ExitApp(clickOk ? 0 : 4)
EOF
XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-0 DISPLAY= \
  AHK_ATSPI_DUMP=/tmp/lo_atspi_dump timeout -k 3 60 \
  "$BIN" /tmp/lo_atspi.ahk "$TITLE_BASE" >/tmp/lo_atspi_ahk.log 2>&1
arc=$?
[ "$arc" = 0 ] && grep -q '^click=1:0$' /tmp/lo_atspi_result \
  || { echo "LIBREOFFICE_ATSPI_ACTION_FAIL rc=$arc"; cat /tmp/lo_atspi_result /tmp/lo_atspi_ahk.log; exit 1; }
header=$(head -1 /tmp/lo_atspi_dump)
cache_apps=$(printf '%s' "$header" | sed -n 's/.*cache_apps=\([0-9][0-9]*\).*/\1/p')
nodes=$(printf '%s' "$header" | sed -n 's/.*nodes=\([0-9][0-9]*\).*/\1/p')
budget_exceeded=$(printf '%s' "$header" | sed -n 's/.*budget_exceeded=\([0-9][0-9]*\).*/\1/p')
main_title="$TITLE_BASE — LibreOffice Calc"
sheet_name="Sheet ${TITLE_BASE%.csv}"
[ "$cache_apps" = 1 ] && [ -n "$nodes" ] && [ "$nodes" -gt 1000 ] \
  && [ "$budget_exceeded" = 0 ] \
  && grep -Fq "$main_title" /tmp/lo_atspi_dump \
  && grep -Fq "$sheet_name" /tmp/lo_atspi_dump \
  && grep -F "$sheet_name" /tmp/lo_atspi_dump | grep -q 'org.a11y.atspi.Table' \
  && ! grep -q '^Text Import -' /tmp/lo_atspi_dump \
  || { echo "LIBREOFFICE_ATSPI_SCOPE_FAIL header=[$header]"; grep -Ei 'Calc|Sheet|Text Import' /tmp/lo_atspi_dump | head -80; exit 1; }

address=$(gdbus call --session --dest org.a11y.Bus --object-path /org/a11y/bus \
  --method org.a11y.Bus.GetAddress | cut -d"'" -f2)
sheet_line=$(grep -F "$sheet_name" /tmp/lo_atspi_dump | grep 'org.a11y.atspi.Table' | head -1)
dest=$(printf '%s' "$sheet_line" | sed -n 's/.*dest=\([^[:space:]]*\).*/\1/p')
path=$(printf '%s' "$sheet_line" | cut -f2)
rows=$(gdbus call --address "$address" --dest "$dest" --object-path "$path" \
  --method org.freedesktop.DBus.Properties.Get org.a11y.atspi.Table NRows \
  | sed -n 's/.*<\([0-9][0-9]*\)>.*/\1/p')
columns=$(gdbus call --address "$address" --dest "$dest" --object-path "$path" \
  --method org.freedesktop.DBus.Properties.Get org.a11y.atspi.Table NColumns \
  | sed -n 's/.*<\([0-9][0-9]*\)>.*/\1/p')
[ "$rows" = 1048576 ] && [ "$columns" = 16384 ] \
  || { echo "LIBREOFFICE_ATSPI_TABLE_FAIL rows=$rows columns=$columns"; exit 1; }
version=$(libreoffice --version | awk '{print $2}')
cat >"$OUT/libreoffice-atspi-summary.json" <<EOF
{"schema":1,"result":"pass","libreoffice":"$version","host":"Calc/GTK3/XWayland","import_dialog_action":true,"main_title":"$main_title","sheet":"$sheet_name","table_rows":$rows,"table_columns":$columns,"cache_apps":$cache_apps,"nodes":$nodes,"budget_exceeded":false,"cell_content_in_cache":false,"table_cell_control_api":false}
EOF
echo "LIBREOFFICE_ATSPI_ORACLE_PASS version=$version action=1 cache_apps=1 nodes=$nodes table=${rows}x${columns} cells=explicit-gap"
