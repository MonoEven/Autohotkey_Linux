# WinSetStyle

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:130](../../tests/doccheck/assert_win.ahk#L130)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Window style is a virtual shadow + EWMH; exact for self-set windows only

Changes the style or extended style of the specified window, respectively.

## Syntax

````text
WinSetStyle Value , WinTitle, WinText, ExcludeTitle, ExcludeText WinSetExStyle Value , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinSetStyle / WinGetStyle, WinSetExStyle / WinGetExStyle. ---
WinSetStyle("+0x800000", "DocCheck Alpha")
Log("style_add=" (WinGetStyle("DocCheck Alpha") = 0x800000 ? 1 : 0))
WinSetStyle("-0x800000", "DocCheck Alpha")
Log("style_sub=" (WinGetStyle("DocCheck Alpha") = 0 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinSetStyle.htm](../../docs-v2/docs/lib/WinSetStyle.htm)

````ahk
WinSetStyle "-0xC00000", "A"
````
