#!/bin/bash
# Run curated examples or the complete Linux-verified profile backing the catalog.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROFILE="${1:-help}"
BIN="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac

run_headless() {
  out=/tmp/ahk-example-runtime-types.txt
  rm -f "$out"
  "$BIN" "$ROOT/examples/language/runtime_types.ahk" "$out"
  grep -q '^array=3 first=10 class=1 enum=1 func=1 string=42 record=linux$' "$out"
  echo EXAMPLE_HEADLESS_PASS
}

run_x11() {
  out=/tmp/ahk-example-gui-controls.txt
  rm -f "$out"
  xvfb-run -a "$BIN" "$ROOT/examples/gui/control_types.ahk" "$out"
  grep -q '^gui_control=.* tree_items=2 list_rows=2$' "$out"
  echo EXAMPLE_X11_PASS
}

run_lifecycle() {
  exit_out=/tmp/ahk-example-exit.txt
  reload_out=/tmp/ahk-example-reload.txt
  reload_marker=/tmp/ahk-example-reload.marker
  rm -f "$exit_out" "$reload_out" "$reload_marker"
  set +e
  "$BIN" "$ROOT/examples/lifecycle/exit_current_thread.ahk" "$exit_out"
  exit_rc=$?
  set -e
  [ "$exit_rc" = 7 ] && grep -q '^before-exit$' "$exit_out" \
    && ! grep -q '^unreachable$' "$exit_out"
  timeout -k 3 20 "$BIN" "$ROOT/examples/lifecycle/reload_once.ahk" \
    "$reload_marker" "$reload_out"
  grep -q '^first-instance$' "$reload_out"
  grep -q '^replacement-instance$' "$reload_out"
  [ ! -e "$reload_marker" ]
  echo EXAMPLE_LIFECYCLE_PASS
}

run_safety() {
  out=/tmp/ahk-example-shutdown-refusal.txt
  rm -f "$out"
  set +e
  "$BIN" "$ROOT/examples/safety/shutdown_requires_confirmation.ahk"
  rc=$?
  set -e
  [ "$rc" = 2 ] && grep -q '^Refused:' "$out"
  echo EXAMPLE_SAFETY_BOUNDARY_PASS
}

case "$PROFILE" in
  catalog-check)
    python3 "$ROOT/tools/gen_examples_catalog.py" --check
    ;;
  headless)
    run_headless
    ;;
  x11)
    run_x11
    ;;
  lifecycle)
    run_lifecycle
    ;;
  safety-boundary)
    run_safety
    ;;
  interactive)
    exec "$BIN" "$ROOT/examples/interactive/input_box.ahk"
    ;;
  all-curated)
    python3 "$ROOT/tools/gen_examples_catalog.py" --check
    run_headless
    run_x11
    run_lifecycle
    run_safety
    echo EXAMPLES_ALL_CURATED_PASS
    ;;
  all-verified)
    exec bash "$ROOT/tests/doccheck/run_check.sh" --xvfb "$BIN"
    ;;
  wayland)
    exec bash "$ROOT/tests/doccheck/wayland_run.sh" "$BIN"
    ;;
  desktop-session)
    exec bash "$ROOT/tests/oracle/run_gui_host_matrix.sh" "$BIN"
    ;;
  help|*)
    cat <<EOF
usage: examples/run.sh PROFILE [AHK_BINARY]
profiles:
  catalog-check   verify all IMPL mappings and generated pages
  headless        run curated language/type examples
  x11             run curated GuiControl/ListView/TreeView example under Xvfb
  lifecycle       run safe Exit and one-shot Reload examples
  safety-boundary prove Shutdown refuses to act without explicit acknowledgement
  interactive     open the InputBox example (requires a person/display)
  all-curated     run every unattended curated profile
  all-verified    run the full X11/doc-check sources backing the catalog
  wayland         run native-Wayland examples/oracles
  desktop-session run the installed real-host matrix
EOF
    [ "$PROFILE" = help ] || exit 2
    ;;
esac
