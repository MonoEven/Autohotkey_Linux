# ExitApp

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_dialog.ahk:67](../../tests/doccheck/assert_dialog.ahk#L67)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_edit.ahk:147](../../tests/doccheck/assert_edit.ahk#L147)
- `x11`: [tests/doccheck/assert_gui.ahk:40](../../tests/doccheck/assert_gui.ahk#L40)
- `x11`: [tests/doccheck/assert_hotkey.ahk:140](../../tests/doccheck/assert_hotkey.ahk#L140)
- `x11`: [tests/doccheck/assert_image.ahk:193](../../tests/doccheck/assert_image.ahk#L193)
- `x11`: [tests/doccheck/assert_msg.ahk:231](../../tests/doccheck/assert_msg.ahk#L231)
- `x11`: [tests/doccheck/assert_shape.ahk:123](../../tests/doccheck/assert_shape.ahk#L123)
- `x11`: [tests/doccheck/assert_timer.ahk:91](../../tests/doccheck/assert_timer.ahk#L91)
- `wayland`: [tests/doccheck/assert_wayland.ahk:139](../../tests/doccheck/assert_wayland.ahk#L139)

Terminates the script.

## Syntax

````text
ExitApp ExitCode
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- Cleanup. ---
ExitApp(0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/ExitApp.htm](../../docs-v2/docs/lib/ExitApp.htm)

````ahk
#x::ExitApp  ; Win+X
````
