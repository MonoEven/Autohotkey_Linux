# ControlGetHwnd

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:110](../../tests/doccheck/assert_ctrl.ahk#L110)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the window handle (HWND) of a control.

## Syntax

````text
HWND := ControlGetHwnd(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ControlSetText("Hello World", "Edit1", "CtlMain")
Log("settext=" (ControlGetText("Edit1", "CtlMain") = "Hello World" ? 1 : 0))
ehwnd := ControlGetHwnd("Edit1", "CtlMain")
Log("gettext_hwnd=" (ControlGetText(ehwnd) = "Hello World" ? 1 : 0))
Log("gettext_ahkid=" (ControlGetText("ahk_id " ehwnd) = "Hello World" ? 1 : 0))
Log("gettext_textmatch=" (ControlGetText("Hello World", "CtlMain") = "Hello World" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetHwnd.htm](../../docs-v2/docs/lib/ControlGetHwnd.htm)

````ahk
MsgBox ControlGetHwnd("Edit1", "Some Window Title")
````
