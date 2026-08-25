#!/bin/bash
# GUI-1/M6 real host matrix: GTK3, Qt6, Java Swing, LibreOffice Calc and
# VS Code/Electron. Each child oracle has an independent target process; this
# script aggregates exact host versions and honest per-host capability levels.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"

bash "$ROOT/tests/oracle/run_atspi_wintitle_oracle.sh" "$BIN" || exit 1
bash "$ROOT/tests/oracle/run_qt6_capture_oracle.sh" "$BIN" || exit 1

JAVA_PRESENT=0
JAVA_VERSION=not-installed
if command -v java >/dev/null && command -v javac >/dev/null \
  && [ -f /usr/share/java/java-atk-wrapper.jar ] \
  && [ -f /usr/lib/x86_64-linux-gnu/jni/libatk-wrapper.so ]; then
  JAVA_PRESENT=1
  JAVA_VERSION=$(java -version 2>&1 | sed -n '1s/.*"\([^"]*\)".*/\1/p')
  bash "$ROOT/tests/oracle/run_java_atspi_oracle.sh" "$BIN" || exit 1
fi

LO_PRESENT=0
LO_VERSION=not-installed
if command -v libreoffice >/dev/null; then
  LO_PRESENT=1
  LO_VERSION=$(libreoffice --version | awk '{print $2}')
  bash "$ROOT/tests/oracle/run_libreoffice_atspi_oracle.sh" "$BIN" || exit 1
fi

bash "$ROOT/tests/oracle/run_vscode_gui_capture_oracle.sh" "$BIN" || exit 1
GTK_VERSION="$(pkg-config --modversion gtk+-3.0 2>/dev/null || echo unknown)"
QT_VERSION="$(pkg-config --modversion Qt6Widgets 2>/dev/null || echo unknown)"
VSCODE_VERSION="$(code --version 2>/dev/null | head -1 || echo unknown)"
python3 - "$OUT/qt6-capture-summary.json" "$OUT/vscode-gui-capture-summary.json" \
  "$OUT/java-atspi-summary.json" "$OUT/libreoffice-atspi-summary.json" \
  "$OUT/gui-host-matrix-summary.json" "$GTK_VERSION" "$QT_VERSION" "$VSCODE_VERSION" \
  "$JAVA_PRESENT" "$JAVA_VERSION" "$LO_PRESENT" "$LO_VERSION" <<'PY'
import json, pathlib, sys
qt = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
code = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
java_present = sys.argv[9] == "1"
lo_present = sys.argv[11] == "1"
out = {
    "schema": 1,
    "result": "pass",
    "hosts": {
        "gtk3": {
            "version": sys.argv[6],
            "x11_window": "covered-by-doccheck",
            "wayland_atspi": "text-read-write-and-wintitle-scope",
            "level": "full-for-tested-controls",
        },
        "qt6": {
            "version": sys.argv[7],
            "x11_window_count": qt["x11_window_count"],
            "wayland_atspi": "text-read-write-action-selection-value",
            "selection_final": qt["selection_final"],
            "value_after": qt["value_after"],
            "level": "full-for-tested-controls",
        },
        "java_swing": {
            "version": sys.argv[10],
            "installed": java_present,
            "level": "not-installed",
        },
        "libreoffice_calc": {
            "version": sys.argv[12],
            "installed": lo_present,
            "level": "not-installed",
        },
        "vscode_electron": {
            "version": sys.argv[8],
            "x11_window_count": code["x11_window_count"],
            "wayland_atspi_window": code["wayland_window"],
            "document_interfaces": code["document_interfaces"],
            "monaco_content_exposed": code["monaco_content_exposed"],
            "level": "window-only-content-unavailable",
        },
    },
}
if java_present:
    java = json.loads(pathlib.Path(sys.argv[3]).read_text(encoding="utf-8"))
    out["hosts"]["java_swing"].update({
        "wayland_atspi": "text-read-write-action-selection-value-read",
        "value_set_effect": java["value_set_effect"],
        "value_set_errno": java["value_set_errno"],
        "level": "tested-controls-except-value-write",
    })
if lo_present:
    calc = json.loads(pathlib.Path(sys.argv[4]).read_text(encoding="utf-8"))
    out["hosts"]["libreoffice_calc"].update({
        "wayland_atspi": "dialog-action-title-scope-table-metadata",
        "table_rows": calc["table_rows"],
        "table_columns": calc["table_columns"],
        "cell_content_in_cache": calc["cell_content_in_cache"],
        "level": "window-and-table-metadata-cell-api-unavailable",
    })
pathlib.Path(sys.argv[5]).write_text(
    json.dumps(out, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8"
)
print(json.dumps(out, ensure_ascii=False, sort_keys=True))
PY
rc=$?
[ "$rc" = 0 ] || exit "$rc"
echo "GUI_HOST_MATRIX_PASS gtk=$GTK_VERSION qt=$QT_VERSION java=$JAVA_VERSION libreoffice=$LO_VERSION vscode=$VSCODE_VERSION vscode_content=unavailable"
