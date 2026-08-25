# WinGetStyle

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:131](../../tests/doccheck/assert_win.ahk#L131)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Window style is a virtual shadow + EWMH; exact for self-set windows only

Returns the style or extended style (respectively) of the specified window.

## Syntax

````text
Style := WinGetStyle(WinTitle, WinText, ExcludeTitle, ExcludeText) ExStyle := WinGetExStyle(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- WinSetStyle / WinGetStyle, WinSetExStyle / WinGetExStyle. ---
WinSetStyle("+0x800000", "DocCheck Alpha")
Log("style_add=" (WinGetStyle("DocCheck Alpha") = 0x800000 ? 1 : 0))
WinSetStyle("-0x800000", "DocCheck Alpha")
Log("style_sub=" (WinGetStyle("DocCheck Alpha") = 0 ? 1 : 0))
WinSetStyle("0x1234", "DocCheck Alpha")
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetStyle.htm](../../docs-v2/docs/lib/WinGetStyle.htm)

````ahk
Style := WinGetStyle("My Window Title")
if (Style & 0x8000000)  ; 0x8000000 is WS_DISABLED.
    MsgBox "The window is disabled."
````
