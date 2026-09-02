# MouseGetPos

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:256](../../tests/doccheck/assert_input.ahk#L256)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves the current position of the mouse cursor, and optionally which window and control it is hovering over.

## Syntax

````text
MouseGetPos &OutputVarX, &OutputVarY, &OutputVarWin, &OutputVarControl, Flag
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MouseMove(300, 200)
Sleep(50)
MouseGetPos(&mx, &my)
Log("mousemove=" (mx = 300 && my = 200 ? 1 : 0))

; --- MouseClick. ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/MouseGetPos.htm](../../docs-v2/docs/lib/MouseGetPos.htm)

````ahk
MouseGetPos &xpos, &ypos
MsgBox "The cursor is at X" xpos " Y" ypos
````
