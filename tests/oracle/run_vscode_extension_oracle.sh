#!/bin/bash
# VSC-1: install the packaged extension into real VS Code, launch an extension
# host under Xvfb, execute ahk_core --diag and one AHK script, and validate the
# JSON marker produced only by the extension-host self-test hook.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
VSIX="${1:-$ROOT/extensions/vscode-ahk-linux/autohotkey-linux-tools-0.1.0.vsix}"
BIN="${2:-$ROOT/build-core/source/linux/core/ahk_core}"
case "$VSIX" in /*) ;; *) VSIX="$ROOT/$VSIX" ;; esac
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
command -v code >/dev/null || { echo "VSCODE_ORACLE_SKIP code-not-installed"; exit 2; }
test -f "$VSIX" || { echo "VSCODE_ORACLE_FAIL missing-vsix=$VSIX"; exit 1; }
test -x "$BIN" || { echo "VSCODE_ORACLE_FAIL missing-runtime=$BIN"; exit 1; }

WORK=/tmp/ahk-vscode-oracle
USER_DATA=/tmp/ahk-vscode-user
EXTENSIONS=/tmp/ahk-vscode-extensions
EVIDENCE=/tmp/ahk-vscode-evidence.json
RUN_MARKER=/tmp/ahk-vscode-run-marker
pkill -f "$USER_DATA" 2>/dev/null
sleep .5
rm -rf "$WORK" "$USER_DATA" "$EXTENSIONS" "$EVIDENCE" "$RUN_MARKER" /tmp/ahk-vscode.log
mkdir -p "$WORK/.vscode" "$USER_DATA" "$EXTENSIONS"
cat >"$WORK/.vscode/settings.json" <<EOF
{
  "ahkLinux.runtime": "$BIN",
  "ahkLinux.inputBackend": "auto",
  "ahkLinux.clearOutputBeforeRun": true,
  "ahkLinux.saveBeforeRun": false,
  "security.workspace.trust.enabled": false
}
EOF
cat >"$WORK/oracle.ahk" <<'EOF'
#Requires AutoHotkey v2.0
FileAppend("VSCODE-RUN-PASS`n", "/tmp/ahk-vscode-run-marker")
ExitApp
EOF

code --user-data-dir "$USER_DATA" --extensions-dir "$EXTENSIONS" \
  --install-extension "$VSIX" --force >/tmp/ahk-vscode-install.log 2>&1 \
  || { echo VSCODE_INSTALL_FAIL; cat /tmp/ahk-vscode-install.log; exit 1; }
installed="$(code --user-data-dir "$USER_DATA" --extensions-dir "$EXTENSIONS" \
  --list-extensions --show-versions | grep '^autohotkey-linux-community.autohotkey-linux-tools@' | head -1)"
[ "$installed" = "autohotkey-linux-community.autohotkey-linux-tools@0.1.0" ] \
  || { echo "VSCODE_LIST_FAIL installed=[$installed]"; exit 1; }

AHK_LINUX_VSCODE_SELFTEST="$EVIDENCE" \
AHK_LINUX_VSCODE_SELFTEST_SCRIPT="$WORK/oracle.ahk" \
xvfb-run -a code --user-data-dir "$USER_DATA" --extensions-dir "$EXTENSIONS" \
  --disable-gpu --no-sandbox --disable-workspace-trust --skip-welcome \
  --verbose --wait --new-window "$WORK" "$WORK/oracle.ahk" >/tmp/ahk-vscode.log 2>&1 &
VPID=$!
for _ in $(seq 1 600); do test -f "$EVIDENCE" && break; sleep .05; done
test -f "$EVIDENCE" || {
  kill "$VPID" 2>/dev/null
  echo VSCODE_EXTENSION_HOST_TIMEOUT
  tail -80 /tmp/ahk-vscode.log 2>/dev/null
  find "$USER_DATA/logs" -type f -name '*.log' -exec grep -Hn 'autohotkey-linux\|ERROR' {} + 2>/dev/null | tail -40
  exit 1
}
kill "$VPID" 2>/dev/null; wait "$VPID" 2>/dev/null
pkill -f "$USER_DATA" 2>/dev/null
python3 - "$EVIDENCE" <<'PY'
import json, pathlib, sys
p = pathlib.Path(sys.argv[1])
data = json.loads(p.read_text(encoding="utf-8"))
assert data["schema"] == 1
assert data["activated"] is True
assert data["extensionVersion"] == "0.1.0"
assert data["languageRegistered"] is True
assert len(data["commands"]) == 5, data
assert data["diagnosticsEntries"] >= 3, data
assert data["runExitCode"] == 0, data
print(json.dumps(data, sort_keys=True))
PY
rc=$?
[ "$rc" = 0 ] || { echo VSCODE_EVIDENCE_FAIL; cat "$EVIDENCE"; exit 1; }
grep -q '^VSCODE-RUN-PASS$' "$RUN_MARKER" \
  || { echo VSCODE_RUN_MARKER_FAIL; cat "$EVIDENCE"; exit 1; }
cp "$EVIDENCE" "$ROOT/tests/oracle/out/vscode-extension-summary.json" 2>/dev/null || true
echo "VSCODE_EXTENSION_ORACLE_PASS installed=$installed"
