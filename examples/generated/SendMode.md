# SendMode

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:98](../../tests/doccheck/assert_sys.ahk#L98)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Makes Send synonymous with SendEvent or SendPlay rather than the default (SendInput). Also makes Click, MouseClick, MouseClickDrag, and MouseMove use the specified mode.

## Syntax

````text
PrevMode := SendMode(Mode)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: "The default sending mode is Input"; SendMode returns the previous mode.
MsgBox "SendMode_prev=" (SendMode("Play") = "Input")
MsgBox "SendMode_set=" (A_SendMode = "Play")
MsgBox "SendMode_return=" (SendMode("InputThenPlay") = "Play")
MsgBox "SendMode_restore=" (SendMode("Input") = "InputThenPlay")
````

## Upstream reference example

Source: [docs-v2/docs/lib/SendMode.htm](../../docs-v2/docs/lib/SendMode.htm)

````ahk
SendMode "InputThenPlay"
````
