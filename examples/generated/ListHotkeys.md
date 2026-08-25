# ListHotkeys

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_display.ahk:51](../../tests/doccheck/assert_display.ahk#L51)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Displays the hotkeys in use by the current script, whether their subroutines are currently running, and whether or not they use the keyboard or mouse hook.

## Syntax

````text
ListHotkeys
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ObjVar := {a: 1}
ListVars()
ListHotkeys()
KeyHistory()
MsgBox("DISPLAY_DONE=1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ListHotkeys.htm](../../docs-v2/docs/lib/ListHotkeys.htm)

````ahk
ListHotkeys
````
