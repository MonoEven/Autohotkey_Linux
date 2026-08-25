# ListVars

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_display.ahk:50](../../tests/doccheck/assert_display.ahk#L50)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Displays the script's variables: their names and current contents.

## Syntax

````text
ListVars
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Count := 42
ObjVar := {a: 1}
ListVars()
ListHotkeys()
KeyHistory()
MsgBox("DISPLAY_DONE=1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ListVars.htm](../../docs-v2/docs/lib/ListVars.htm)

````ahk
var1 := "foo"
var2 := "bar"
obj := []
ListVars
Pause
````
