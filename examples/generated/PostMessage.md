# PostMessage

- Linux status: `IMPL` (P4)
- Example kind: `verified-error`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_msg.ahk:66](../../tests/doccheck/assert_msg.ahk#L66)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: No Win32 message system on X11: raises the documented error / returns 0

Places a message in the message queue of a window or control.

## Syntax

````text
PostMessage MsgNumber , wParam, lParam, ControlID, WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- PostMessage: same validation, no return value ---
PostMessage(0x1234, 5, 6, "Edit1", "MsgMain")
Log("pm_ctrl=1")
PostMessage(0x1234)
Log("pm_lf=1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/PostMessage.htm](../../docs-v2/docs/lib/PostMessage.htm)

````ahk
PostMessage 0x0050, 0, 0x4090409,, "A"  ; 0x0050 is WM_INPUTLANGCHANGEREQUEST.
````
