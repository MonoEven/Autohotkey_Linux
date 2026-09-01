# GetKeyState

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:303](../../tests/doccheck/assert_input.ahk#L303)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `wayland`: [tests/doccheck/assert_wayland.ahk:73](../../tests/doccheck/assert_wayland.ahk#L73)

Returns 1 (true) or 0 (false) depending on whether the specified keyboard key or mouse/controller button is down or up. Also retrieves controller status.

## Syntax

````text
IsDown := GetKeyState(KeyName , Mode)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- SetCapsLockState/SetNumLockState/SetScrollLockState + GetKeyState "T". ---
SetCapsLockState("On")
Log("caps_on=" (GetKeyState("CapsLock", "T") = 1 ? 1 : 0))
SetCapsLockState(-1)
Log("caps_toggle=" (GetKeyState("CapsLock", "T") = 0 ? 1 : 0))
SetCapsLockState(1)
````

## Upstream reference example

Source: [docs-v2/docs/lib/GetKeyState.htm](../../docs-v2/docs/lib/GetKeyState.htm)

````ahk
state := GetKeyState("RButton")
````
