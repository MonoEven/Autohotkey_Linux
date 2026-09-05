# VS Code VM Visual Smoke

Date: 2026-09-05
Host: Linux VM `mono@192.168.111.130`
Desktop path: GNOME Wayland session with XWayland `:0`
VS Code: 1.134.0
Extension: `autohotkey-linux-community.autohotkey-linux-tools@0.2.1`
Runtime: `build-core/source/linux/core/ahk_core`

## Completed Operations

1. Installed `autohotkey-linux-tools-0.2.1.vsix` into an isolated VS Code profile.
2. Opened `vscode-visual/demo.ahk`; AHK v2 language registration and syntax highlighting were visible.
3. Pressed F6 (`AutoHotkey Linux: Run Current File`); the Output panel showed the exact runtime command and the script marker recorded `result=15`.
4. Ran `AutoHotkey Linux: Show Runtime Diagnostics`; the Output panel showed backend, GNOME, portal, libei and uinput diagnostics.
5. Navigated to line 3, toggled a gutter breakpoint, pressed F5, and observed the paused debug UI with Variables, Call Stack, Debug Console and the highlighted line 3.
6. Opened `broken.ahk` and displayed the Problems panel. The visual flow did not produce a runtime diagnostic marker because F6 was not accepted in that isolated second window; the screenshot is retained as a negative result, not a pass claim.

## Evidence

The VM flow produced these local screenshots under `vscode_visual_evidence/`:

- `01-open.png`
- `02-run.png`
- `03-diagnostics.png`
- `04-debug-breakpoint.png`
- `05-error-diagnostics.png`
- `06-broken-open.png`
- `07-broken-output.png`
- `08-broken-problems.png`

`operations.log` records installation, window discovery, F6 marker, diagnostics command, debug start and screenshot creation. `error-operations.log` records the separate broken-file attempt.

## Plugin Assessment

The plugin runtime version remains `0.2.1`; its manifest already contains the required language, run, diagnostics, capability-tree and inline DAP contributions. A feature-version bump is not justified by this smoke test. The repository oracle was updated to derive its expected version from the tested VSIX instead of hardcoding `0.2.0`.

The Wayland VS Code accessibility capture remains environment-limited on this VM: the window is visible, but the VS Code AT-SPI node exposes no Text/Document interfaces under the current Electron/desktop combination. XWayland visual operation and the extension-host/DAP oracle are successful.
