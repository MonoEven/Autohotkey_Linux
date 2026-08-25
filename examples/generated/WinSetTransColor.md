# WinSetTransColor

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:117](../../tests/doccheck/assert_win.ahk#L117)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: The TransColor color-key concept has no X11 equivalent (approximated)

Makes all pixels of the chosen color invisible inside the specified window.

## Syntax

````text
WinSetTransColor Color , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinSetTransColor / WinGetTransColor (stored; no X11 equivalent). ---
WinSetTransColor("0x112233", "DocCheck Alpha")
Log("transcolor_set=" (WinGetTransColor("DocCheck Alpha") = "0x112233" ? 1 : 0))
WinSetTransColor("Off", "DocCheck Alpha")
Log("transcolor_off=" (WinGetTransColor("DocCheck Alpha") = "" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinSetTransColor.htm](../../docs-v2/docs/lib/WinSetTransColor.htm)

````ahk
WinSetTransColor "White", "Untitled - Notepad"
````
