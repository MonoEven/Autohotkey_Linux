# WinSetTitle

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:90](../../tests/doccheck/assert_win.ahk#L90)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Changes the title of the specified window.

## Syntax

````text
WinSetTitle NewTitle , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinSetTitle. ---
WinSetTitle("DocCheck Alpha Renamed", "DocCheck Alpha")
Log("winsettitle=" (WinGetTitle("DocCheck Alpha Renamed") = "DocCheck Alpha Renamed"))
WinSetTitle("DocCheck Alpha", "DocCheck Alpha Renamed")
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinSetTitle.htm](../../docs-v2/docs/lib/WinSetTitle.htm)

````ahk
WinSetTitle("This is a new title", "Untitled - Notepad")
````
