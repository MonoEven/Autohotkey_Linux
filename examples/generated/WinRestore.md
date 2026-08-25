# WinRestore

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:97](../../tests/doccheck/assert_win.ahk#L97)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Unminimizes or unmaximizes the specified window if it is minimized or maximized.

## Syntax

````text
WinRestore WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
WinMinimize("DocCheck Alpha")
Log("minmax_min=" (WinGetMinMax("DocCheck Alpha") = -1 ? 1 : 0))
WinRestore("DocCheck Alpha")
Log("minmax_restored=" (WinGetMinMax("DocCheck Alpha") = 0 ? 1 : 0))
WinMaximize("DocCheck Alpha")
Log("minmax_max=" (WinGetMinMax("DocCheck Alpha") = 1 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinRestore.htm](../../docs-v2/docs/lib/WinRestore.htm)

````ahk
WinRestore "Untitled - Notepad"
````
