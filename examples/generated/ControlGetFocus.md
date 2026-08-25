# ControlGetFocus

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:144](../../tests/doccheck/assert_ctrl.ahk#L144)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves which control of the target window has keyboard focus, if any.

## Syntax

````text
HWND := ControlGetFocus(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- ControlFocus / ControlGetFocus (returns HWND per docs). ---
ControlFocus("Edit2", "CtlMain")
focus_hwnd := ControlGetFocus("CtlMain")
Log("focus=" (focus_hwnd = ControlGetHwnd("Edit2", "CtlMain") ? 1 : 0))
Log("focus_classnn=" (ControlGetClassNN(focus_hwnd) = "Edit2" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetFocus.htm](../../docs-v2/docs/lib/ControlGetFocus.htm)

````ahk
FocusedHwnd := ControlGetFocus("A")
FocusedClassNN := ControlGetClassNN(FocusedHwnd)
MsgBox 'Control with focus = {Hwnd: ' FocusedHwnd ', ClassNN: "' FocusedClassNN '"}'
````
