# WinGetTransColor

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:118](../../tests/doccheck/assert_win.ahk#L118)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the color that is marked transparent in the specified window.

## Syntax

````text
TransColor := WinGetTransColor(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- WinSetTransColor / WinGetTransColor (stored; no X11 equivalent). ---
WinSetTransColor("0x112233", "DocCheck Alpha")
Log("transcolor_set=" (WinGetTransColor("DocCheck Alpha") = "0x112233" ? 1 : 0))
WinSetTransColor("Off", "DocCheck Alpha")
Log("transcolor_off=" (WinGetTransColor("DocCheck Alpha") = "" ? 1 : 0))
try {
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetTransColor.htm](../../docs-v2/docs/lib/WinGetTransColor.htm)

````ahk
MouseGetPos ,, &MouseWin
TransColor := WinGetTransColor(MouseWin)
````
