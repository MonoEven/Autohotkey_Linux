# SendMessage

- Linux status: `IMPL` (P4)
- Example kind: `verified-error`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_msg.ahk:31](../../tests/doccheck/assert_msg.ahk#L31)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: No Win32 message system on X11: raises the documented error / returns 0

Sends a message to a window or control and waits for acknowledgement.

## Syntax

````text
Result := SendMessage(MsgNumber , wParam, lParam, ControlID, WinTitle, WinText, ExcludeTitle, ExcludeText, Timeout)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- SendMessage: returns 0 (the DefWindowProc default reply) ---
Log("sm_ctrl=" (SendMessage(0x1234, 0, 0, "Edit1", "MsgMain") = 0 ? 1 : 0))
Log("sm_win=" (SendMessage(0x1234, 5, 6, , "MsgMain") = 0 ? 1 : 0)) ; Control omitted: to the window.
Log("sm_lf=" (SendMessage(0x1234) = 0 ? 1 : 0)) ; Last Found Window (WinWait).
Log("sm_timeout=" (SendMessage(0x1234, 0, 0, "Edit1", "MsgMain", "", "", "", 50) = 0 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/SendMessage.htm](../../docs-v2/docs/lib/SendMessage.htm)

````ahk
#o::  ; Win+O hotkey
{
    Sleep 1000  ; Give user a chance to release keys (in case their release would wake up the monitor again).
    ; Turn Monitor Off:
    SendMessage 0x0112, 0xF170, 2,, "Program Manager"  ; 0x0112 is WM_SYSCOMMAND, 0xF170 is SC_MONITORPOWER.
}
````
