# SendLevel

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotkey.ahk:15](../../tests/doccheck/assert_hotkey.ahk#L15)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:18](../../tests/doccheck/assert_hotkey_lr.ahk#L18)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:23](../../tests/doccheck/assert_hotkey_pt.ahk#L23)
- `x11`: [tests/doccheck/assert_hotstring.ahk:36](../../tests/doccheck/assert_hotstring.ahk#L36)
- `x11`: [tests/doccheck/assert_input.ahk:193](../../tests/doccheck/assert_input.ahk#L193)
- `x11`: [tests/doccheck/assert_inputhook.ahk:73](../../tests/doccheck/assert_inputhook.ahk#L73)
- `x11`: [tests/doccheck/assert_layout.ahk:12](../../tests/doccheck/assert_layout.ahk#L12)
- `x11`: [tests/doccheck/assert_repeat.ahk:18](../../tests/doccheck/assert_repeat.ahk#L18)
- `headless`: [tests/doccheck/assert_sys.ahk:108](../../tests/doccheck/assert_sys.ahk#L108)

Controls which artificial keyboard and mouse events are ignored by hotkeys and hotstrings.

## Syntax

````text
PrevLevel := SendLevel(Level)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; SendEvent-class input at level 1 (the Windows-golden way to trigger own
; hotkeys) so these assertions test hotkey semantics, not the level gate.
SendLevel(1)
SendMode("Event")

; --- Simple hotkey fires when its key is pressed. ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/SendLevel.htm](../../docs-v2/docs/lib/SendLevel.htm)

````ahk
SendLevel 1
SendEvent "btw{Space}" ; Produces "by the way ".
; This may be defined in a separate script:
::btw::by the way
````
