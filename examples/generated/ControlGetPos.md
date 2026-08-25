# ControlGetPos

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:127](../../tests/doccheck/assert_ctrl.ahk#L127)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves the position and size of a control.

## Syntax

````text
ControlGetPos &OutX, &OutY, &OutWidth, &OutHeight, ControlID, WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- ControlGetPos / ControlMove. ---
ControlGetPos(&cx, &cy, &cw, &ch, "Button1", "CtlMain")
Log("getpos=" (cx = 200 && cy = 40 && cw = 90 && ch = 30 ? 1 : 0))
ControlMove(250, 50, , , "Button1", "CtlMain")
ControlGetPos(&cx, &cy, &cw, &ch, "Button1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetPos.htm](../../docs-v2/docs/lib/ControlGetPos.htm)

````ahk
Loop
{
    Sleep 100
    MouseGetPos ,, &WhichWindow, &WhichControl
    try ControlGetPos &x, &y, &w, &h, WhichControl, WhichWindow
    ToolTip WhichControl "`nX" X "`tY" Y "`nW" W "`t" H
}
````
