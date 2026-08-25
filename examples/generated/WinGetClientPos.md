# WinGetClientPos

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:75](../../tests/doccheck/assert_win.ahk#L75)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves the position and size of the specified window's client area.

## Syntax

````text
WinGetClientPos &OutX, &OutY, &OutWidth, &OutHeight, WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
WinGetPos(&x, &y, &w, &h, "DocCheck Alpha")
Log("getpos=" (x = 100 && y = 100 && w = 400 && h = 300 ? 1 : 0))
WinGetClientPos(&cx, &cy, &cw, &ch, "DocCheck Alpha")
Log("getclientpos=" (cx = 100 && cy = 100 && cw = 400 && ch = 300 ? 1 : 0))

; --- WinGetText / WinGetControls / WinGetControlsHwnd (no X11 controls). ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetClientPos.htm](../../docs-v2/docs/lib/WinGetClientPos.htm)

````ahk
WinGetClientPos &X, &Y, &W, &H, "Calculator"
MsgBox "Calculator's client area is at " X "," Y " and its size is " W "x" H
````
