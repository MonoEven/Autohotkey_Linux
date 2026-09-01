# Hotkey

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotkey.ahk:24](../../tests/doccheck/assert_hotkey.ahk#L24)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:55](../../tests/doccheck/assert_hotkey_btn.ahk#L55)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:57](../../tests/doccheck/assert_hotkey_lr.ahk#L57)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:57](../../tests/doccheck/assert_hotkey_pt.ahk#L57)
- `x11`: [tests/doccheck/assert_input.ahk:188](../../tests/doccheck/assert_input.ahk#L188)
- `x11`: [tests/doccheck/assert_layout.ahk:57](../../tests/doccheck/assert_layout.ahk#L57)
- `x11`: [tests/doccheck/assert_repeat.ahk:65](../../tests/doccheck/assert_repeat.ahk#L65)
- `wayland`: [tests/doccheck/assert_wayland.ahk:107](../../tests/doccheck/assert_wayland.ahk#L107)

Creates, modifies, enables, or disables a hotkey while the script is running.

## Syntax

````text
Hotkey KeyName , Action, Options
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    cnt1++
}
Hotkey("F7", CB1)
Sleep(200) ; Let the grab be established.
Send("{F7}")
Sleep(300)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Hotkey.htm](../../docs-v2/docs/lib/Hotkey.htm)

````ahk
Hotkey "^!z", MyFunc
MyFunc(ThisHotkey)
{
    MsgBox "You pressed " ThisHotkey
}
````
