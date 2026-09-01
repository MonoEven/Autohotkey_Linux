# MouseMove

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:246](../../tests/doccheck/assert_input.ahk#L246)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `wayland`: [tests/doccheck/assert_wayland.ahk:88](../../tests/doccheck/assert_wayland.ahk#L88)

Moves the mouse cursor.

## Syntax

````text
MouseMove X, Y , Speed, Relative
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- MouseMove + MouseGetPos. ---
MouseMove(300, 200)
Sleep(50)
MouseGetPos(&mx, &my)
Log("mousemove=" (mx = 300 && my = 200 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/MouseMove.htm](../../docs-v2/docs/lib/MouseMove.htm)

````ahk
MouseMove 200, 100
````
