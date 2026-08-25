# WinMaximize

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:99](../../tests/doccheck/assert_win.ahk#L99)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Enlarges the specified window to its maximum size.

## Syntax

````text
WinMaximize WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
WinRestore("DocCheck Alpha")
Log("minmax_restored=" (WinGetMinMax("DocCheck Alpha") = 0 ? 1 : 0))
WinMaximize("DocCheck Alpha")
Log("minmax_max=" (WinGetMinMax("DocCheck Alpha") = 1 ? 1 : 0))
WinRestore("DocCheck Alpha")
Log("minmax_norm=" (WinGetMinMax("DocCheck Alpha") = 0 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinMaximize.htm](../../docs-v2/docs/lib/WinMaximize.htm)

````ahk
Run "notepad.exe"
WinWait "Untitled - Notepad"
WinMaximize ; Use the window found by WinWait.
````
