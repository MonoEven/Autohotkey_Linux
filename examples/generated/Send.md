# Send

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotkey.ahk:19](../../tests/doccheck/assert_hotkey.ahk#L19)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:70](../../tests/doccheck/assert_hotkey_btn.ahk#L70)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:61](../../tests/doccheck/assert_hotkey_lr.ahk#L61)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:66](../../tests/doccheck/assert_hotkey_pt.ahk#L66)
- `x11`: [tests/doccheck/assert_hotstring.ahk:35](../../tests/doccheck/assert_hotstring.ahk#L35)
- `x11`: [tests/doccheck/assert_input.ahk:58](../../tests/doccheck/assert_input.ahk#L58)
- `x11`: [tests/doccheck/assert_inputhook.ahk:22](../../tests/doccheck/assert_inputhook.ahk#L22)
- `x11`: [tests/doccheck/assert_layout.ahk:57](../../tests/doccheck/assert_layout.ahk#L57)
- `x11`: [tests/doccheck/assert_repeat.ahk:64](../../tests/doccheck/assert_repeat.ahk#L64)
- `wayland`: [tests/doccheck/assert_wayland.ahk:39](../../tests/doccheck/assert_wayland.ahk#L39)

Sends simulated keystrokes and mouse clicks to the active window.

## Syntax

````text
Send Keys SendText Keys SendEvent Keys SendInput Keys SendPlay Keys
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Hotkey("F7", CB1)
Sleep(200) ; Let the grab be established.
Send("{F7}")
Sleep(300)
Log("hk_f7=" (cnt1 = 1 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/Send.htm](../../docs-v2/docs/lib/Send.htm)

````ahk
Send "Sincerely,{enter}John Smith"
````
