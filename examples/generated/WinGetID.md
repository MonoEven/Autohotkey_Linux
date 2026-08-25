# WinGetID

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:163](../../tests/doccheck/assert_ctrl.ahk#L163)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_shape.ahk:24](../../tests/doccheck/assert_shape.ahk#L24)
- `x11`: [tests/doccheck/assert_win.ahk:64](../../tests/doccheck/assert_win.ahk#L64)

Returns the unique ID (HWND) of the specified window.

## Syntax

````text
HWND := WinGetID(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Sleep(80)
Log("click_xy=" (btns(next_lines()) = "down:1,up:1" ? 1 : 0))
main_hwnd := WinGetID("CtlMain")
; The position-mode click lands on the main window itself.
ControlClick("x350 y250", "CtlMain")
Sleep(80)
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetID.htm](../../docs-v2/docs/lib/WinGetID.htm)

````ahk
active_id := WinGetID("A")
WinMaximize active_id
MsgBox "The active window's HWND is " active_id
````
