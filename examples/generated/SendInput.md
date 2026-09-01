# SendInput

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:113](../../tests/doccheck/assert_input.ahk#L113)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: XTEST batch without a journal; hook-unload self-suppression implemented, target delivery limited by the passive grab

## Additional verified environments

- `headless`: [tests/doccheck/assert_notimpl.ahk:39](../../tests/doccheck/assert_notimpl.ahk#L39)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Sleep(60)
Log("sendevent=" (downs(next_lines()) = "x" ? 1 : 0))
SendInput("y")
Sleep(60)
Log("sendinput=" (downs(next_lines()) = "y" ? 1 : 0))
SendPlay("z")
````
