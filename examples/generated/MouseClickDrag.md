# MouseClickDrag

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:278](../../tests/doccheck/assert_input.ahk#L278)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Clicks and holds the specified mouse button, moves the mouse to the destination coordinates, then releases the button.

## Syntax

````text
MouseClickDrag WhichButton, X1, Y1, X2, Y2 , Speed, Relative
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- MouseClickDrag. ---
MouseClickDrag("Left", 10, 10, 120, 90)
Sleep(80)
Log("mousedrag_btns=" (btns(next_lines()) = "down:1,up:1" ? 1 : 0))
MouseGetPos(&mx, &my)
````

## Upstream reference example

Source: [docs-v2/docs/lib/MouseClickDrag.htm](../../docs-v2/docs/lib/MouseClickDrag.htm)

````ahk
MouseClickDrag "left", 0, 200, 600, 400
````
