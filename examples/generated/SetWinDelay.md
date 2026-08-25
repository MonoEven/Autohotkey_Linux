# SetWinDelay

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:84](../../tests/doccheck/assert_sys.ahk#L84)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Sets the delay that will occur after each windowing function, such as WinActivate.

## Syntax

````text
PrevDelay := SetWinDelay(Delay)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: SetWinDelay returns the previous delay (default 100).
MsgBox "WinDelay_prev=" (SetWinDelay(50) = 100)
MsgBox "WinDelay_set=" (A_WinDelay = 50)
MsgBox "WinDelay_return=" (SetWinDelay(100) = 50)
; Docs: SetControlDelay returns the previous delay (default 20).
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetWinDelay.htm](../../docs-v2/docs/lib/SetWinDelay.htm)

````ahk
SetWinDelay 10
````
