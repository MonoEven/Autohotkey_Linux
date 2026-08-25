# Sleep

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:28](../../tests/doccheck/assert_clipboard.ahk#L28)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_clipboard_slow.ahk:24](../../tests/doccheck/assert_clipboard_slow.ahk#L24)
- `x11`: [tests/doccheck/assert_ctrl.ahk:20](../../tests/doccheck/assert_ctrl.ahk#L20)
- `x11`: [tests/doccheck/assert_edit.ahk:34](../../tests/doccheck/assert_edit.ahk#L34)
- `x11`: [tests/doccheck/assert_hotkey.ahk:18](../../tests/doccheck/assert_hotkey.ahk#L18)
- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:53](../../tests/doccheck/assert_hotkey_btn.ahk#L53)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:51](../../tests/doccheck/assert_hotkey_lr.ahk#L51)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:51](../../tests/doccheck/assert_hotkey_pt.ahk#L51)
- `x11`: [tests/doccheck/assert_hotstring.ahk:21](../../tests/doccheck/assert_hotstring.ahk#L21)
- `x11`: [tests/doccheck/assert_image.ahk:47](../../tests/doccheck/assert_image.ahk#L47)
- `x11`: [tests/doccheck/assert_input.ahk:18](../../tests/doccheck/assert_input.ahk#L18)
- `x11`: [tests/doccheck/assert_inputhook.ahk:17](../../tests/doccheck/assert_inputhook.ahk#L17)
- `x11`: [tests/doccheck/assert_layout.ahk:14](../../tests/doccheck/assert_layout.ahk#L14)
- `x11`: [tests/doccheck/assert_monitor.ahk:16](../../tests/doccheck/assert_monitor.ahk#L16)
- `x11`: [tests/doccheck/assert_msg.ahk:28](../../tests/doccheck/assert_msg.ahk#L28)
- `x11`: [tests/doccheck/assert_repeat.ahk:21](../../tests/doccheck/assert_repeat.ahk#L21)
- `x11`: [tests/doccheck/assert_shape.ahk:23](../../tests/doccheck/assert_shape.ahk#L23)
- `host-tools`: [tests/doccheck/assert_sound_etc.ahk:52](../../tests/doccheck/assert_sound_etc.ahk#L52)
- `x11`: [tests/doccheck/assert_timer.ahk:21](../../tests/doccheck/assert_timer.ahk#L21)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:19](../../tests/doccheck/assert_unicode_lease.ahk#L19)
- `wayland`: [tests/doccheck/assert_wayland.ahk:40](../../tests/doccheck/assert_wayland.ahk#L40)
- `x11`: [tests/doccheck/assert_win.ahk:23](../../tests/doccheck/assert_win.ahk#L23)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:237](../../tests/doccheck/assert_misc_cov.ahk#L237)

Waits the specified amount of time before continuing.

## Syntax

````text
Sleep Delay
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    i := 0
    while !FileExist(TBASE ".txt") && i < 60 {
        Sleep(50)
        i += 1
    }
    t := FileExist(TBASE ".txt") ? FileRead(TBASE ".txt") : ""
````

## Upstream reference example

Source: [docs-v2/docs/lib/Sleep.htm](../../docs-v2/docs/lib/Sleep.htm)

````ahk
Sleep 1000
````
