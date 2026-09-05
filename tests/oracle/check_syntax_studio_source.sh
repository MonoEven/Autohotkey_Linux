#!/bin/bash
# Static guard for the runnable syntax-teaching GUI example.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/examples/gui/syntax_studio.ahk"
for needle in \
  'lesson10 := Lesson(10' \
  'Gui("+Resize", "AHK v2 Syntax Studio")' \
  '"ListBox"' \
  'AddDropDownList' \
  '"TreeView"' \
  'AddEdit' \
  '"StatusBar"' \
  'FilterLessons' \
  'RunPractice' \
  'CheckPractice' \
  'SetTimer(WatchLessonSelection, 50)'; do
  grep -Fq "$needle" "$SRC"
done
for image in \
  syntax_studio_overview.png \
  syntax_studio_conditions.png \
  syntax_studio_loops_filter.png \
  syntax_studio_check.png \
  syntax_studio_run.png; do
  test -s "$ROOT/examples/gui/screenshots/$image"
done
echo 'SYNTAX_STUDIO_STATIC_PASS lessons=10 controls=1 filter=1 practice=1 screenshots=5'
