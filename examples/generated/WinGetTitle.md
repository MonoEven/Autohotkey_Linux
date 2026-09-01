# WinGetTitle

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:138](../../tests/doccheck/assert_ctrl.ahk#L138)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_input.ahk:290](../../tests/doccheck/assert_input.ahk#L290)
- `x11`: [tests/doccheck/assert_timer.ahk:82](../../tests/doccheck/assert_timer.ahk#L82)
- `wayland`: [tests/doccheck/assert_wayland.ahk:101](../../tests/doccheck/assert_wayland.ahk#L101)
- `x11`: [tests/doccheck/assert_win.ahk:46](../../tests/doccheck/assert_win.ahk#L46)

Retrieves the title of the specified window.

## Syntax

````text
Title := WinGetTitle(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- ControlGetHwnd / ControlGetClassNN. ---
bhwnd := ControlGetHwnd("Button1", "CtlMain")
Log("gethwnd_win=" (WinGetTitle("ahk_id " bhwnd) = "ButtonOK" ? 1 : 0))
Log("getclassnn_hwnd=" (ControlGetClassNN(bhwnd) = "Button1" ? 1 : 0))
Log("getclassnn_spec=" (ControlGetClassNN("Button1", "CtlMain") = "Button1" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetTitle.htm](../../docs-v2/docs/lib/WinGetTitle.htm)

````ahk
MsgBox "The active window is '" WinGetTitle("A") "'."
````
