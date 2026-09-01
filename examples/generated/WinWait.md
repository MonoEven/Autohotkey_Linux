# WinWait

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:19](../../tests/doccheck/assert_ctrl.ahk#L19)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_edit.ahk:33](../../tests/doccheck/assert_edit.ahk#L33)
- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:52](../../tests/doccheck/assert_hotkey_btn.ahk#L52)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:54](../../tests/doccheck/assert_hotkey_lr.ahk#L54)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:54](../../tests/doccheck/assert_hotkey_pt.ahk#L54)
- `x11`: [tests/doccheck/assert_hotstring.ahk:20](../../tests/doccheck/assert_hotstring.ahk#L20)
- `x11`: [tests/doccheck/assert_image.ahk:46](../../tests/doccheck/assert_image.ahk#L46)
- `x11`: [tests/doccheck/assert_input.ahk:24](../../tests/doccheck/assert_input.ahk#L24)
- `x11`: [tests/doccheck/assert_inputhook.ahk:21](../../tests/doccheck/assert_inputhook.ahk#L21)
- `x11`: [tests/doccheck/assert_layout.ahk:17](../../tests/doccheck/assert_layout.ahk#L17)
- `x11`: [tests/doccheck/assert_monitor.ahk:15](../../tests/doccheck/assert_monitor.ahk#L15)
- `x11`: [tests/doccheck/assert_msg.ahk:27](../../tests/doccheck/assert_msg.ahk#L27)
- `x11`: [tests/doccheck/assert_repeat.ahk:24](../../tests/doccheck/assert_repeat.ahk#L24)
- `x11`: [tests/doccheck/assert_shape.ahk:22](../../tests/doccheck/assert_shape.ahk#L22)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:17](../../tests/doccheck/assert_unicode_lease.ahk#L17)
- `x11`: [tests/doccheck/assert_win.ahk:19](../../tests/doccheck/assert_win.ahk#L19)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:235](../../tests/doccheck/assert_misc_cov.ahk#L235)

Waits until the specified window exists.

## Syntax

````text
HWND := WinWait(WinTitle, WinText, Timeout, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    ' -child ButtonOK Button 200 40 90 30 -child ComboBox1 ComboBox 30 120 150 24'
    ' -child Hidden1 Hidden 10 10 20 20')
WinWait("CtlMain",, 5)
Sleep(300)

; Read the lines appended to the event file since the last call.
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinWait.htm](../../docs-v2/docs/lib/WinWait.htm)

````ahk
Run "notepad.exe"
if WinWait("Untitled - Notepad", , 3)
    WinMinimize ; Use the window found by WinWait.
else
    MsgBox "WinWait timed out."
````
