# SetControlDelay

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:88](../../tests/doccheck/assert_sys.ahk#L88)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Sets the delay that will occur after each control-modifying function.

## Syntax

````text
PrevDelay := SetControlDelay(Delay)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "WinDelay_return=" (SetWinDelay(100) = 50)
; Docs: SetControlDelay returns the previous delay (default 20).
MsgBox "ControlDelay_prev=" (SetControlDelay(30) = 20)
MsgBox "ControlDelay_set=" (A_ControlDelay = 30)
MsgBox "ControlDelay_return=" (SetControlDelay(20) = 30)
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetControlDelay.htm](../../docs-v2/docs/lib/SetControlDelay.htm)

````ahk
SetControlDelay 0
````
