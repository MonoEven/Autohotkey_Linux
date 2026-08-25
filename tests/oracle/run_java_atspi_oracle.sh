#!/bin/bash
# M6 real-host matrix: OpenJDK Swing through Java ATK Wrapper on GNOME XWayland,
# controlled from a display-less AHK process over AT-SPI.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"
for command in java javac; do
  command -v "$command" >/dev/null || { echo "JAVA_ATSPI_SKIP missing-$command"; exit 2; }
done
JAR=/usr/share/java/java-atk-wrapper.jar
JNI=/usr/lib/x86_64-linux-gnu/jni/libatk-wrapper.so
[ -f "$JAR" ] && [ -f "$JNI" ] \
  || { echo JAVA_ATSPI_SKIP java-atk-wrapper-missing; exit 2; }
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus"
auth=$(ls -1t "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
display=$(ps -u "$(id -u)" -o args= | sed -n 's/.*Xwayland \(:[0-9][0-9]*\).*/\1/p' | head -1)
[ -n "$auth" ] && [ -n "$display" ] \
  || { echo JAVA_ATSPI_SKIP gnome-xwayland-session-missing; exit 2; }
CLASSES=/tmp/ahk-java-atspi-classes
rm -rf "$CLASSES"
mkdir -p "$CLASSES"
javac -d "$CLASSES" "$ROOT/tests/oracle/JavaAtspiProbe.java" || exit 2
rm -f /tmp/java_atspi_{ready,title,text,button,selection,value,full,host.log,dump}
JPID=0
cleanup() {
  [ "$JPID" = 0 ] || kill "$JPID" 2>/dev/null || true
  [ "$JPID" = 0 ] || wait "$JPID" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
DISPLAY="$display" XAUTHORITY="$auth" NO_AT_BRIDGE=0 GTK_MODULES=gail:atk-bridge \
  java -Djava.awt.headless=false \
    -Djavax.accessibility.assistive_technologies=org.GNOME.Accessibility.AtkWrapper \
    -Djava.library.path=/usr/lib/x86_64-linux-gnu/jni \
    -cp "$CLASSES:$JAR" JavaAtspiProbe >/tmp/java_atspi_host.log 2>&1 &
JPID=$!
for _ in $(seq 1 300); do
  test -s /tmp/java_atspi_ready && test -s /tmp/java_atspi_title && break
  sleep .05
done
test -s /tmp/java_atspi_ready && kill -0 "$JPID" 2>/dev/null \
  || { echo JAVA_ATSPI_HOST_START_FAIL; cat /tmp/java_atspi_host.log; exit 1; }
# Swing's visible marker precedes Java ATK Wrapper's registry/cache registration.
sleep 2

cat >/tmp/java_atspi.ahk <<'EOF'
#Requires AutoHotkey v2.0
title := StrReplace(StrReplace(FileRead("/tmp/java_atspi_title"), "`r"), "`n")
before := ControlGetText("JAVA-ENTRY", title)
ControlSetText("Java-新值", "JAVA-ENTRY", title)
after := ControlGetText("JAVA-ENTRY", title)
ControlClick("JAVA-BUTTON", title)
items := ControlGetItems("JAVA-LIST", title)
ControlChooseIndex(2, "JAVA-LIST", title)
choice := ControlGetChoice("JAVA-LIST", title)
index := ControlGetIndex("JAVA-LIST", title)
valueBefore := ControlGetText("JAVA-SLIDER", title)
valueFailed := 0
valueFailureCode := 0
try ControlSetText("64", "JAVA-SLIDER", title)
catch OSError
{
    valueFailed := 1
    valueFailureCode := A_LastError
}
valueAfter := ControlGetText("JAVA-SLIDER", title)
FileAppend("title=" title
    "`nbefore=" before "`nafter=" after
    "`nitems=" items.Length ":" items[1] ":" items[2] ":" items[3]
    "`nchoice=" choice ":" index
    "`nvalue=" valueBefore ":" valueAfter ":" valueFailed ":" valueFailureCode "`n",
    "/tmp/java_atspi_full")
ExitApp
EOF
rm -f /tmp/java_atspi_full /tmp/java_atspi_dump
XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-0 DISPLAY= \
  AHK_ATSPI_DUMP=/tmp/java_atspi_dump timeout -k 3 60 \
  "$BIN" /tmp/java_atspi.ahk >/tmp/java_atspi_ahk.log 2>&1
arc=$?
[ "$arc" = 0 ] || { echo "JAVA_ATSPI_AHK_FAIL rc=$arc"; cat /tmp/java_atspi_ahk.log; exit 1; }
for _ in $(seq 1 100); do
  test -s /tmp/java_atspi_text && test -s /tmp/java_atspi_button \
    && test -s /tmp/java_atspi_selection && break
  sleep .02
done
grep -q '^before=Java-世界$' /tmp/java_atspi_full \
  && grep -q '^after=Java-新值$' /tmp/java_atspi_full \
  && grep -q '^items=3:Alpha:Bravo:世界$' /tmp/java_atspi_full \
  && grep -q '^choice=Bravo:2$' /tmp/java_atspi_full \
  && grep -q '^value=25:25:1:5$' /tmp/java_atspi_full \
  || { echo JAVA_ATSPI_SEMANTICS_FAIL; cat /tmp/java_atspi_full /tmp/java_atspi_ahk.log; exit 1; }
grep -q '^Java-新值$' /tmp/java_atspi_text \
  && grep -q '^clicked$' /tmp/java_atspi_button \
  && grep -q '^Bravo$' /tmp/java_atspi_selection \
  && [ ! -e /tmp/java_atspi_value ] \
  || { echo JAVA_ATSPI_EFFECT_FAIL; cat /tmp/java_atspi_text /tmp/java_atspi_button /tmp/java_atspi_selection /tmp/java_atspi_value; exit 1; }
header=$(head -1 /tmp/java_atspi_dump)
cache_apps=$(printf '%s' "$header" | sed -n 's/.*cache_apps=\([0-9][0-9]*\).*/\1/p')
nodes=$(printf '%s' "$header" | sed -n 's/.*nodes=\([0-9][0-9]*\).*/\1/p')
budget_exceeded=$(printf '%s' "$header" | sed -n 's/.*budget_exceeded=\([0-9][0-9]*\).*/\1/p')
[ "$cache_apps" = 1 ] && [ -n "$nodes" ] && [ "$nodes" -gt 10 ] \
  && [ "$budget_exceeded" = 0 ] \
  && grep -q '^JAVA-ENTRY.*EditableText.*Text' /tmp/java_atspi_dump \
  && grep -q '^JAVA-BUTTON.*Action' /tmp/java_atspi_dump \
  && grep -q '^JAVA-LIST.*Selection' /tmp/java_atspi_dump \
  && grep -q '^JAVA-SLIDER.*Value' /tmp/java_atspi_dump \
  || { echo "JAVA_ATSPI_CACHE_SCOPE_FAIL header=[$header]"; grep -E 'JAVA-|AHK Java' /tmp/java_atspi_dump; exit 1; }
java_version=$(java -version 2>&1 | sed -n '1s/.*"\([^"]*\)".*/\1/p')
wrapper_version=$(dpkg-query -W -f='${Version}' libatk-wrapper-java 2>/dev/null)
title=$(cat /tmp/java_atspi_title)
cat >"$OUT/java-atspi-summary.json" <<EOF
{"schema":1,"result":"pass","java":"$java_version","java_atk_wrapper":"$wrapper_version","host":"Swing/XWayland","title":"$title","text_before":"Java-世界","text_after":"Java-新值","button":true,"items":["Alpha","Bravo","世界"],"selection":"Bravo","value_read":25,"value_set_effect":false,"value_set_errno":5,"cache_apps":$cache_apps,"nodes":$nodes,"budget_exceeded":false}
EOF
echo "JAVA_ATSPI_ORACLE_PASS java=$java_version text=1 action=1 selection=1 value=explicit-EIO cache_apps=1 nodes=$nodes"
