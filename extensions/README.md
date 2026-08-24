# AutoHotkey Linux extension products

## VS Code — `vscode-ahk-linux`

Source: `extensions/vscode-ahk-linux/`

VSC-1 is a native Linux runtime companion, not a repackaged Windows-only debugger:

- AutoHotkey v2 language registration, syntax grammar and editor configuration;
- run file/selection, stop processes, task provider and problem matcher;
- runtime error diagnostics;
- backend status bar and `ahk_core --diag` capability tree;
- X11/portal/GNOME/evdev and optional `ahk-inputd` settings;
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
  extensions/vscode-ahk-linux/autohotkey-linux-tools-0.1.0.vsix \
  build-core/source/linux/core/ahk_core
```

The oracle installs the VSIX into isolated VS Code user/extension directories, starts a real extension host under Xvfb, checks language and command registration, parses runtime diagnostics and executes an AHK script. VSC-1 explicitly does not claim interactive breakpoints/stepping; the DBGp/DAP adapter is VSC-2.

## GNOME Shell — global-hotkey broker extension

The existing extension is in `extension/ahk-global-hotkeys@autohotkey.org/`. It is a desktop-global hotkey transport, separate from the VS Code tooling.
