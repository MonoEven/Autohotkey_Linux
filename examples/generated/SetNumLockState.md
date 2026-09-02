# SetNumLockState

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:318](../../tests/doccheck/assert_input.ahk#L318)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
SetCapsLockState("Off")
Log("caps_off=" (GetKeyState("CapsLock", "T") = 0 ? 1 : 0))
SetNumLockState("On")
Log("num_on=" (GetKeyState("NumLock", "T") = 1 ? 1 : 0))
SetNumLockState("Off")
Log("num_off=" (GetKeyState("NumLock", "T") = 0 ? 1 : 0))
````
