# WinMinimizeAll

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:171](../../tests/doccheck/assert_win.ahk#L171)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Minimizes or unminimizes all windows.

## Syntax

````text
WinMinimizeAll WinMinimizeAllUndo
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinMinimizeAll / WinMinimizeAllUndo. ---
WinMinimizeAll()
Sleep(50)
Log("minall=" (WinGetMinMax("DocCheck Alpha") = -1 ? 1 : 0))
WinMinimizeAllUndo()
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinMinimizeAll.htm](../../docs-v2/docs/lib/WinMinimizeAll.htm)

````ahk
WinMinimizeAll
Sleep 1000
WinMinimizeAllUndo
````
