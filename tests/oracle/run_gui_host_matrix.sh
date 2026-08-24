#!/bin/bash
# GUI-1 real host matrix: GTK3, Qt6 and VS Code/Electron. Each child oracle
# has an independent target process; this script aggregates exact host versions
# and the honest per-host capability level into one stable JSON result.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"

bash "$ROOT/tests/oracle/run_atspi_wintitle_oracle.sh" "$BIN" || exit 1
bash "$ROOT/tests/oracle/run_qt6_capture_oracle.sh" "$BIN" || exit 1
bash "$ROOT/tests/oracle/run_vscode_gui_capture_oracle.sh" "$BIN" || exit 1

GTK_VERSION="$(pkg-config --modversion gtk+-3.0 2>/dev/null || echo unknown)"
QT_VERSION="$(pkg-config --modversion Qt6Widgets 2>/dev/null || echo unknown)"
VSCODE_VERSION="$(code --version 2>/dev/null | head -1 || echo unknown)"
python3 - "$OUT/qt6-capture-summary.json" "$OUT/vscode-gui-capture-summary.json" \
  "$OUT/gui-host-matrix-summary.json" "$GTK_VERSION" "$QT_VERSION" "$VSCODE_VERSION" <<'PY'
import json, pathlib, sys
qt = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
code = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
out = {
    "schema": 1,
    "result": "pass",
    "hosts": {
        "gtk3": {
            "version": sys.argv[4],
            "x11_window": "covered-by-doccheck",
            "wayland_atspi": "text-read-write-and-wintitle-scope",
            "level": "full-for-tested-controls",
        },
        "qt6": {
            "version": sys.argv[5],
            "x11_window_count": qt["x11_window_count"],
            "wayland_atspi": "text-read-write-action-selection-value",
            "selection_final": qt["selection_final"],
            "value_after": qt["value_after"],
            "level": "full-for-tested-controls",
        },
        "vscode_electron": {
            "version": sys.argv[6],
            "x11_window_count": code["x11_window_count"],
            "wayland_atspi_window": code["wayland_window"],
            "document_interfaces": code["document_interfaces"],
            "monaco_content_exposed": code["monaco_content_exposed"],
            "level": "window-only-content-unavailable",
        },
    },
}
pathlib.Path(sys.argv[3]).write_text(json.dumps(out, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8")
print(json.dumps(out, ensure_ascii=False, sort_keys=True))
PY
rc=$?
[ "$rc" = 0 ] || exit "$rc"
echo "GUI_HOST_MATRIX_PASS gtk=$GTK_VERSION qt=$QT_VERSION vscode=$VSCODE_VERSION vscode_content=unavailable"
