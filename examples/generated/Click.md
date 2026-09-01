# Click

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_input.ahk:277](../../tests/doccheck/assert_input.ahk#L277)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Clicks a mouse button at the specified coordinates. It can also hold down a mouse button, turn the mouse wheel, or move the mouse.

## Syntax

````text
Click Options
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- Click (g_BIF). ---
Click("Right")
Sleep(80)
Log("click_right=" (btns(next_lines()) = "down:3,up:3" ? 1 : 0))
Click("150 160")
````

## Upstream reference example

Source: [docs-v2/docs/lib/Click.htm](../../docs-v2/docs/lib/Click.htm)

````ahk
Click
````
