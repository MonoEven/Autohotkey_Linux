# ControlMove

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:129](../../tests/doccheck/assert_ctrl.ahk#L129)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Moves and/or resizes a control.

## Syntax

````text
ControlMove X, Y, Width, Height, ControlID, WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ControlGetPos(&cx, &cy, &cw, &ch, "Button1", "CtlMain")
Log("getpos=" (cx = 200 && cy = 40 && cw = 90 && ch = 30 ? 1 : 0))
ControlMove(250, 50, , , "Button1", "CtlMain")
ControlGetPos(&cx, &cy, &cw, &ch, "Button1", "CtlMain")
Log("move_xy=" (cx = 250 && cy = 50 && cw = 90 && ch = 30 ? 1 : 0))
ControlMove(, , 110, 40, "Button1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlMove.htm](../../docs-v2/docs/lib/ControlMove.htm)

````ahk
SetTimer ControlMoveTimer
IB := InputBox(, "My Input Box")
ControlMoveTimer()
{
    if !WinExist("My Input Box")
        return
    ; Otherwise the above set the "last found" window for us:
    SetTimer , 0
    WinActivate
    ControlMove 10,, 200,, "OK"  ; Move the OK button to th
````
