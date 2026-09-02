#!/bin/bash
# verify_report_numbers.sh -- machine-check the assertion counts published in
# CHECK_REPORT.md against the expect files.
#
# check0819 flagged that the report's numbers drifted by hand (Wayland "847",
# XWayland 235 vs 247, module rows out of sync with the expect files).  The
# expect files are the single source of truth for what run_check.sh /
# wayland_run.sh assert, so this script recomputes every published count and
# fails when the report disagrees.  CI runs it after the suites pass, making
# stale hand-maintained numbers impossible.
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR" || exit 1
REPORT=CHECK_REPORT.md
fail=0

count_lines() { # file -> number of non-empty lines
  if [ ! -f "$1" ]; then
    echo "MISSING $1" >&2
    return 0
  fi
  grep -cv '^[[:space:]]*$' "$1"
}

# --- ground truth from the expect files --------------------------------
x11=0
wayland=$(count_lines assert_wayland_expect.txt)
for f in assert_*_expect.txt; do
  [ "$f" = "assert_wayland_expect.txt" ] && continue
  x11=$((x11 + $(count_lines "$f")))
done
x11=$((x11 + $(count_lines assert_display_content.txt)))
xw=0
for b in ctrl edit dialog msg shape image hotkey; do
  xw=$((xw + $(count_lines "assert_${b}_expect.txt")))
done

# --- CHECK_REPORT.md rows ----------------------------------------------
# The FIRST "| 模块 | ... |" table only (a later status table reuses the
# header; its "✅ N/N |" cells must not be parsed).  The 合计 row itself is
# included (it carries the published total).
rows=$(awk '/^\| 模块 \|/{f=1; next}
            f && /^\| \*\*合计 \(X11/{print; exit}
            f{print}' "$REPORT" | tr -d '\r')

check_row() { # label want  (label = start of the row, may contain "(")
  local got
  got=$(printf '%s\n' "$rows" | grep -F "| **$1" | head -1 \
        | grep -oE '\*\*[0-9]+\*\*' | tr -d '*' | head -1)
  if [ "$got" != "$2" ]; then
    echo "FAIL: CHECK_REPORT '$1' row = '$got', expect '$2'"
    fail=1
  fi
}
check_summary() { # want
  local got
  got=$(grep -oE '\*\*[0-9]+ / [0-9]+ 断言通过\*\*' "$REPORT" | head -1 \
        | grep -oE '[0-9]+' | head -1)
  if [ "$got" != "$1" ]; then
    echo "FAIL: summary total '$got' != '$1'"
    fail=1
  fi
}
check_pass() { # want  (section 5 block: PASS=<n> FAIL=0 for both builds)
  if ! grep -q "PASS=$1 FAIL=0" "$REPORT"; then
    echo "FAIL: section 5 does not contain PASS=$1 FAIL=0"
    fail=1
  fi
}

# Every per-module table row "| ... | `assert_X.ahk` | N |" must match the
# expect file (assert_display = expect + content patterns).
while IFS= read -r row; do
  base=$(printf '%s' "$row" | grep -oE 'assert_[a-z0-9_]+\.ahk' | head -1 | sed 's/\.ahk//')
  [ -n "$base" ] || continue
  want=$(printf '%s' "$row" | grep -oE '\| [0-9]+ \|$' | tr -d '| ')
  exp=$(count_lines "${base}_expect.txt")
  if [ "$base" = "assert_display" ]; then
    exp=$((exp + $(count_lines assert_display_content.txt)))
  fi
  if [ "$want" != "$exp" ]; then
    echo "FAIL: table row $base says '$want', expect file has '$exp'"
    fail=1
  fi
done <<< "$rows"

check_row "合计 (X11/headless" "$x11"
# The Wayland 合计 row lives after the X11 table (outside the awk range).
got=$(grep -F "| **合计 (Wayland)" "$REPORT" | head -1 \
      | grep -oE '\*\*[0-9]+\*\*' | tr -d '*' | head -1)
if [ "$got" != "$wayland" ]; then
  echo "FAIL: CHECK_REPORT '合计 (Wayland)' row = '$got', expect '$wayland'"
  fail=1
fi
check_summary "$x11"
check_pass "$x11"
check_pass "$wayland"
check_pass "$xw"
tr -d '\r' < "$REPORT" \
  | grep -qE "^\| XWayland 回退 .*\| $xw \|$" \
  || { echo "FAIL: XWayland row != $xw"; fail=1; }

echo "verify_report_numbers: x11=$x11 wayland=$wayland xwayland=$xw"
[ "$fail" = 0 ]