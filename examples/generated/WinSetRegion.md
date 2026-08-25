# WinSetRegion

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_shape.ahk:38](../../tests/doccheck/assert_shape.ahk#L38)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `wayland`: [tests/doccheck/assert_wayland.ahk:119](../../tests/doccheck/assert_wayland.ahk#L119)

Changes the shape of the specified window to be the specified rectangle, ellipse, or polygon.

## Syntax

````text
WinSetRegion Options, WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- Rectangle: "0-0 W100 H50" -> one 100x50 rect at the origin. ---
WinSetRegion("0-0 W100 H50", "ShpMain")
Sleep(200)
s := ProbeShape()
Log("wsr_rect=" (InStr(s, "shaped=1") && InStr(s, "rect 0 0 100 50") ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinSetRegion.htm](../../docs-v2/docs/lib/WinSetRegion.htm)

````ahk
WinSetRegion "50-0 w200 h250", "ahk_class Notepad"
````
