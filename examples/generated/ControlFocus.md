# ControlFocus

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:143](../../tests/doccheck/assert_ctrl.ahk#L143)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Sets keyboard focus to a control.

## Syntax

````text
ControlFocus ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
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

Source: [docs-v2/docs/lib/ControlFocus.htm](../../docs-v2/docs/lib/ControlFocus.htm)

````ahk
ControlFocus "OK", "Some Window Title"
````
