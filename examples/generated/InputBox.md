# InputBox

- Linux status: `IMPL` (P1)
- Example kind: `interactive`
- Environment profile: `interactive`
- Verified source: [examples/interactive/input_box.ahk:3](../../examples/interactive/input_box.ahk#L3)
- Profile command: `"$BIN" examples/interactive/input_box.ahk`

Displays an input box to ask the user to enter a string.

## Syntax

````text
InputBoxObj := InputBox(Prompt, Title, Options, Default)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
#Requires AutoHotkey v2.0
; Interactive example: this intentionally waits for a person.
result := InputBox("Enter a value to echo:", "AutoHotkey Linux InputBox", "w420 h140", "世界")
if result.Result = "OK"
    MsgBox("You entered: " result.Value)
else
````

## Upstream reference example

Source: [docs-v2/docs/lib/InputBox.htm](../../docs-v2/docs/lib/InputBox.htm)

````ahk
password := InputBox("(your input will be hidden)", "Enter Password", "password").value
````

## Interaction

This example intentionally waits for user input and is excluded from unattended runs.
