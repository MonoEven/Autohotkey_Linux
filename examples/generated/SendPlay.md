# SendPlay

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:116](../../tests/doccheck/assert_input.ahk#L116)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: No X11 journal: SendEvent pacing + the SetKeyDelay ,, Play variants (injection depth equals Event)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Sleep(60)
Log("sendinput=" (downs(next_lines()) = "y" ? 1 : 0))
SendPlay("z")
Sleep(60)
Log("sendplay=" (downs(next_lines()) = "z" ? 1 : 0))
````
