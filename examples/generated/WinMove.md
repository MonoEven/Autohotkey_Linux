# WinMove

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:84](../../tests/doccheck/assert_win.ahk#L84)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Changes the position and/or size of the specified window.

## Syntax

````text
WinMove X, Y, Width, Height, WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinMove (docs: X/Y/Width/Height then WinTitle). ---
WinMove(150, 160, 420, 310, "DocCheck Alpha")
WinGetPos(&x, &y, &w, &h, "DocCheck Alpha")
Log("winmove=" (x = 150 && y = 160 && w = 420 && h = 310 ? 1 : 0))
WinMove(100, 100, 400, 300, "DocCheck Alpha")
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinMove.htm](../../docs-v2/docs/lib/WinMove.htm)

````ahk
Run "calc.exe"
WinWait "Calculator"
WinMove 0, 0 ; Use the window found by WinWait.
````
