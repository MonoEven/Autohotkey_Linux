# ControlGetClassNN

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:139](../../tests/doccheck/assert_ctrl.ahk#L139)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the ClassNN (class name and sequence number) of a control.

## Syntax

````text
ClassNN := ControlGetClassNN(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
bhwnd := ControlGetHwnd("Button1", "CtlMain")
Log("gethwnd_win=" (WinGetTitle("ahk_id " bhwnd) = "ButtonOK" ? 1 : 0))
Log("getclassnn_hwnd=" (ControlGetClassNN(bhwnd) = "Button1" ? 1 : 0))
Log("getclassnn_spec=" (ControlGetClassNN("Button1", "CtlMain") = "Button1" ? 1 : 0))

; --- ControlFocus / ControlGetFocus (returns HWND per docs). ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetClassNN.htm](../../docs-v2/docs/lib/ControlGetClassNN.htm)

````ahk
MsgBox ControlGetClassNN(ControlGetFocus("A"))
````
