# MouseClick

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:242](../../tests/doccheck/assert_input.ahk#L242)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `wayland`: [tests/doccheck/assert_wayland.ahk:90](../../tests/doccheck/assert_wayland.ahk#L90)

Clicks or holds down a mouse button, or turns the mouse wheel. Note: The Click function is generally more flexible and easier to use.

## Syntax

````text
MouseClick WhichButton, X, Y, ClickCount, Speed, DownOrUp, Relative
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- MouseClick. ---
MouseClick("Left", 100, 100)
Sleep(80)
Log("mouseclick_btns=" (btns(next_lines()) = "down:1,up:1" ? 1 : 0))
MouseGetPos(&mx, &my)
````

## Upstream reference example

Source: [docs-v2/docs/lib/MouseClick.htm](../../docs-v2/docs/lib/MouseClick.htm)

````ahk
MouseClick "left"
MouseClick "left"
````
