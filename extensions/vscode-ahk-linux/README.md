# AutoHotkey Linux Tools for VS Code

A small, dependency-free VS Code extension for the native AutoHotkey v2 Linux port.

## Features

- AutoHotkey v2 language registration, comments, brackets, folding and TextMate syntax highlighting.
- Run the current `.ahk`/`.ah2` file with a configurable native `ahk_core` executable (`F6`).
- Run a selected fragment through a temporary v2 script.
- Stop scripts launched by the extension (`Shift+F6`).
- Task provider (`type: "ahk-linux"`) and problem matcher.
- Runtime error output promoted to VS Code diagnostics.
- `ahk_core --diag` output channel, status-bar backend indicator and **AHK Linux Capabilities** Explorer tree.
- Input lane configuration (`auto`, X11, portal, GNOME Shell or evdev) and optional `ahk-inputd` socket.
- Native interactive debugger (`type: "ahk-linux"`): filename breakpoints, continue,
  step into/over/out, call stack, Local/Global scopes, scalar variables/evaluate
  and clean termination through the Linux DBGp core.

## Configure the runtime

```json
{
  "ahkLinux.runtime": "/home/me/Autohotkey_Linux/build-core/source/linux/core/ahk_core",
  "ahkLinux.inputBackend": "auto",
  "ahkLinux.inputdSocket": ""
}
```

For Remote SSH, Dev Containers or WSL, install the extension in the remote extension host and point `ahkLinux.runtime` to the runtime in that environment. The extension is declared `workspace`-host only and is disabled in untrusted or virtual workspaces because running AHK executes workspace code and requires a native filesystem/process.

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

## Debugging

Press F5 on an AHK v2 file or add this launch configuration:

```json
{
  "type": "ahk-linux",
  "request": "launch",
  "name": "Debug current AutoHotkey file",
  "program": "${file}"
}
```

VSC-2 D1 (runtime DBGp) and D2 (inline DBGp-to-DAP adapter) are both
independently exercised, including a real VS Code 1.134 extension-host run.
D3 object trees and exception breakpoints are implemented: Array/Map/Object
expansion supports paging/nested handles, and caught exceptions expose their
message before continuing. Persistent idle scripts can also be paused and expose an explicitly labelled
no-active-frame Global scope. Linux D-Bus compatibility objects expose
side-effect-free adapter metadata. Use **Detach Debugger (Keep Script Running)**
and **Reconnect Last Detached Debuggee** to reattach to the same PID/endpoint;
an IDE socket crash leaves the script running detached. See
[VSC2_DEBUGGER_PORT.md](VSC2_DEBUGGER_PORT.md) for the three-layer evidence.

## Package and test

```bash
npm test
npm run package
```

`npm run package` creates a `.vsix` with the pinned official `@vscode/vsce` packager.
