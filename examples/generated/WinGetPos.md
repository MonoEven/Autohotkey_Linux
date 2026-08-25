# WinGetPos

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_timer.ahk:85](../../tests/doccheck/assert_timer.ahk#L85)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_win.ahk:73](../../tests/doccheck/assert_win.ahk#L73)

Retrieves the position and size of the specified window.

## Syntax

````text
WinGetPos &OutX, &OutY, &OutWidth, &OutHeight, WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
h2 := ToolTip("Updated Tip", 300, 200)
Log("tip_update=" (h1 = h2 ? 1 : 0))
WinGetPos(&tx, &ty, , , "ahk_id " h2)
Log("tip_pos=" (tx >= 299 && tx <= 302 && ty >= 199 && ty <= 202 ? 1 : 0))
h3 := ToolTip()
Log("tip_hide=" (h3 = 0 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetPos.htm](../../docs-v2/docs/lib/WinGetPos.htm)

````ahk
WinGetPos &X, &Y, &W, &H, "Calculator"
MsgBox "Calculator is at " X "," Y " and its size is " W "x" H
````
