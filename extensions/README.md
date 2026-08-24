# AutoHotkey Linux extension products

## VS Code — `vscode-ahk-linux`

Source: `extensions/vscode-ahk-linux/`

The extension is a native Linux runtime companion with its own verified DBGp/DAP debugger:

- AutoHotkey v2 language registration, syntax grammar and editor configuration;
- run file/selection, stop processes, task provider and problem matcher;
- runtime error diagnostics;
- backend status bar and `ahk_core --diag` capability tree;
- X11/portal/GNOME/evdev and optional `ahk-inputd` settings;
- filename breakpoints, stack/scopes/scalars, continue and stepping through an
  inline DAP adapter over the native Linux DBGp core;
- pinned `@vscode/vsce` VSIX packaging.

Tests:

```bash
cd extensions/vscode-ahk-linux
npm test
npm run package
```

VM extension-host oracle:

```bash
bash tests/oracle/run_vscode_extension_oracle.sh \
  extensions/vscode-ahk-linux/autohotkey-linux-tools-0.2.0.vsix \
  build-core/source/linux/core/ahk_core
```

The oracle installs the VSIX into isolated directories, starts a real VS Code extension host, checks language/commands/diagnostics and executes an AHK script. It then starts the contributed debugger, observes breakpoint line 3, step line 4, `x=10`, `y=15` and clean termination through a DebugAdapterTracker.

## GNOME Shell — global-hotkey broker extension

The existing extension is in `extension/ahk-global-hotkeys@autohotkey.org/`. It is a desktop-global hotkey transport, separate from the VS Code tooling.
