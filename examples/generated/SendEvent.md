# SendEvent

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:103](../../tests/doccheck/assert_input.ahk#L103)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- SendMode variants: all deliver XTEST events on Linux. ---
SendEvent("x")
Sleep(60)
Log("sendevent=" (downs(next_lines()) = "x" ? 1 : 0))
SendInput("y")
````
