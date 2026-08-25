# OnMessage

- Linux status: `IMPL` (P4)
- Example kind: `verified-error`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_msg.ahk:89](../../tests/doccheck/assert_msg.ahk#L89)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: No Win32 message system on X11: monitors are stored but nothing is delivered

Registers a function to be called automatically whenever the script receives the specified message.

## Syntax

````text
OnMessage MsgNumber, Callback , MaxThreads
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- OnMessage: register / re-register / unregister monitors ---
CB(wParam, lParam, msg, hwnd) => 0
OnMessage(0x1000, CB)
Log("om_reg=1")
OnMessage(0x1000, CB, 1) ; Option 2 (docs): registered after existing ones.
Log("om_again=1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/OnMessage.htm](../../docs-v2/docs/lib/OnMessage.htm)

````ahk
MyGui := Gui(, "Example Window")
MyGui.Add("Text",, "Click anywhere in this window.")
MyGui.Add("Edit", "w200")
MyGui.Show()
OnMessage 0x0201, WM_LBUTTONDOWN
WM_LBUTTONDOWN(wParam, lParam, msg, hwnd)
{
    X := lParam & 0xFFFF
    Y := lParam >> 16
    Control := ""
    thisGui :
````
