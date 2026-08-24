# AutoHotkey Linux Tools for VS Code

A small, dependency-free VS Code extension for the native AutoHotkey v2 Linux port.

## VSC-1 features

- AutoHotkey v2 language registration, comments, brackets, folding and TextMate syntax highlighting.
- Run the current `.ahk`/`.ah2` file with a configurable native `ahk_core` executable (`F6`).
- Run a selected fragment through a temporary v2 script.
- Stop scripts launched by the extension (`Shift+F6`).
- Task provider (`type: "ahk-linux"`) and problem matcher.
- Runtime error output promoted to VS Code diagnostics.
- `ahk_core --diag` output channel, status-bar backend indicator and **AHK Linux Capabilities** Explorer tree.
- Input lane configuration (`auto`, X11, portal, GNOME Shell or evdev) and optional `ahk-inputd` socket.

## Configure the runtime

```json
{
  "ahkLinux.runtime": "/home/me/Autohotkey_Linux/build-core/source/linux/core/ahk_core",
  "ahkLinux.inputBackend": "auto",
  "ahkLinux.inputdSocket": ""
}
```

For Remote SSH, Dev Containers or WSL, install the extension in the remote extension host and point `ahkLinux.runtime` to the runtime in that environment.

## Task example

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Run current AHK (evdev)",
      "type": "ahk-linux",
      "script": "${file}",
      "backend": "evdev",
      "problemMatcher": "$ahk-linux"
    }
  ]
}
```

## Debugging status

VSC-1 provides execution, process stopping, problem diagnostics and runtime/backend inspection. It does **not** claim an interactive debugger: breakpoints, stepping, variable inspection and DBGp/DAP transport belong to VSC-2 and will be enabled only after a Linux debugger adapter passes an end-to-end extension-host test. Existing third-party AutoHotkey DBGp extensions may still be configured manually if their runtime protocol works in the user's environment.

## Package and test

```bash
npm test
npm run package
```

`npm run package` creates a `.vsix` with the pinned official `@vscode/vsce` packager.
