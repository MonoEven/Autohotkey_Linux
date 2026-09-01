# SendMode

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotkey.ahk:16](../../tests/doccheck/assert_hotkey.ahk#L16)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:19](../../tests/doccheck/assert_hotkey_lr.ahk#L19)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:24](../../tests/doccheck/assert_hotkey_pt.ahk#L24)
- `x11`: [tests/doccheck/assert_hotstring.ahk:37](../../tests/doccheck/assert_hotstring.ahk#L37)
- `x11`: [tests/doccheck/assert_input.ahk:17](../../tests/doccheck/assert_input.ahk#L17)
- `x11`: [tests/doccheck/assert_inputhook.ahk:16](../../tests/doccheck/assert_inputhook.ahk#L16)
- `x11`: [tests/doccheck/assert_layout.ahk:13](../../tests/doccheck/assert_layout.ahk#L13)
- `x11`: [tests/doccheck/assert_repeat.ahk:19](../../tests/doccheck/assert_repeat.ahk#L19)
- `headless`: [tests/doccheck/assert_sys.ahk:98](../../tests/doccheck/assert_sys.ahk#L98)

Makes Send synonymous with SendEvent or SendPlay rather than the default (SendInput). Also makes Click, MouseClick, MouseClickDrag, and MouseMove use the specified mode.

## Syntax

````text
PrevMode := SendMode(Mode)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; hotkeys) so these assertions test hotkey semantics, not the level gate.
SendLevel(1)
SendMode("Event")

; --- Simple hotkey fires when its key is pressed. ---
cnt1 := 0
````

## Upstream reference example

Source: [docs-v2/docs/lib/SendMode.htm](../../docs-v2/docs/lib/SendMode.htm)

````ahk
SendMode "InputThenPlay"
````
