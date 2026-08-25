# BlockInput

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:316](../../tests/doccheck/assert_input.ahk#L316)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Disables or enables the user's ability to interact with the computer via keyboard and mouse.

## Syntax

````text
BlockInput OnOff BlockInput SendMouse BlockInput MouseMove
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; keyboard state, not the capture file).
next_lines()
BlockInput("On")
Send("{Enter}")
Sleep(60)
Log("block_send=" (downs(next_lines()) = "" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/BlockInput.htm](../../docs-v2/docs/lib/BlockInput.htm)

````ahk
BlockInput true
Run "notepad"
WinWaitActive "ahk_class Notepad"
Send "{F5}" ; pastes time and date
BlockInput false
````
