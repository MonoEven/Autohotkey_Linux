# SetMouseDelay

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:71](../../tests/doccheck/assert_sys.ahk#L71)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Sets the delay that will occur after each mouse movement or click.

## Syntax

````text
PrevDelay := SetMouseDelay(Delay , "Play")
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: SetMouseDelay returns the previous delay (default 10).
MsgBox "MouseDelay_prev=" (SetMouseDelay(20) = 10)
MsgBox "MouseDelay_set=" (A_MouseDelay = 20)
mousedelay_play_prev := SetMouseDelay(30, "Play")
MsgBox "MouseDelay_play=" (mousedelay_play_prev = -1 && A_MouseDelayPlay = 30)
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetMouseDelay.htm](../../docs-v2/docs/lib/SetMouseDelay.htm)

````ahk
SetMouseDelay 0
````
