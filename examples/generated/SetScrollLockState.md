# SetScrollLockState

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:314](../../tests/doccheck/assert_input.ahk#L314)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
SetNumLockState("Off")
Log("num_off=" (GetKeyState("NumLock", "T") = 0 ? 1 : 0))
SetScrollLockState("On")
Log("scroll_on=" (GetKeyState("ScrollLock", "T") = 1 ? 1 : 0))
SetScrollLockState("Off")
Log("scroll_off=" (GetKeyState("ScrollLock", "T") = 0 ? 1 : 0))
````
