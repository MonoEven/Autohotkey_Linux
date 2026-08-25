# WinMinimize

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:95](../../tests/doccheck/assert_win.ahk#L95)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Collapses the specified window into a button on the task bar.

## Syntax

````text
WinMinimize WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinMinimize / WinMaximize / WinRestore + WinGetMinMax. ---
WinMinimize("DocCheck Alpha")
Log("minmax_min=" (WinGetMinMax("DocCheck Alpha") = -1 ? 1 : 0))
WinRestore("DocCheck Alpha")
Log("minmax_restored=" (WinGetMinMax("DocCheck Alpha") = 0 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinMinimize.htm](../../docs-v2/docs/lib/WinMinimize.htm)

````ahk
Run "notepad.exe"
WinWait "Untitled - Notepad"
WinMinimize ; Use the window found by WinWait.
````
