# WinGetTransparent

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:112](../../tests/doccheck/assert_win.ahk#L112)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Window transparency is a virtual shadow + _NET_WM_WINDOW_OPACITY; external windows not guaranteed

Returns the degree of transparency of the specified window.

## Syntax

````text
TransDegree := WinGetTransparent(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- WinSetTransparent / WinGetTransparent. ---
WinSetTransparent(128, "DocCheck Alpha")
Log("transparent_set=" (WinGetTransparent("DocCheck Alpha") = 128 ? 1 : 0))
WinSetTransparent("Off", "DocCheck Alpha")
Log("transparent_off=" (WinGetTransparent("DocCheck Alpha") = "" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetTransparent.htm](../../docs-v2/docs/lib/WinGetTransparent.htm)

````ahk
MouseGetPos ,, &MouseWin
TransDegree := WinGetTransparent(MouseWin)
````
