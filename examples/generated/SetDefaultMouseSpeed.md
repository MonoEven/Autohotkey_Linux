# SetDefaultMouseSpeed

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:93](../../tests/doccheck/assert_sys.ahk#L93)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Sets the mouse speed that will be used if unspecified in Click, MouseMove, MouseClick, and MouseClickDrag.

## Syntax

````text
PrevSpeed := SetDefaultMouseSpeed(Speed)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: SetDefaultMouseSpeed returns the previous speed (default 2).
MsgBox "DefaultMouseSpeed_prev=" (SetDefaultMouseSpeed(5) = 2)
MsgBox "DefaultMouseSpeed_set=" (A_DefaultMouseSpeed = 5)
MsgBox "DefaultMouseSpeed_return=" (SetDefaultMouseSpeed(2) = 5)
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetDefaultMouseSpeed.htm](../../docs-v2/docs/lib/SetDefaultMouseSpeed.htm)

````ahk
SetDefaultMouseSpeed 0
````
