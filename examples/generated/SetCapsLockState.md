# SetCapsLockState

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:302](../../tests/doccheck/assert_input.ahk#L302)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- SetCapsLockState/SetNumLockState/SetScrollLockState + GetKeyState "T". ---
SetCapsLockState("On")
Log("caps_on=" (GetKeyState("CapsLock", "T") = 1 ? 1 : 0))
SetCapsLockState(-1)
Log("caps_toggle=" (GetKeyState("CapsLock", "T") = 0 ? 1 : 0))
````
