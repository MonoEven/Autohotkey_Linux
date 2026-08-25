# GetKeyName

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:108](../../tests/doccheck/assert_general.ahk#L108)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Retrieves the name/text of a key.

## Syntax

````text
Name := GetKeyName(KeyName)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; Sort / VerCompare / Type already covered elsewhere; spot checks:
MsgBox "VerCompare=" VerCompare("2.0.26", "2.0.20")
MsgBox "GetKeyName=" GetKeyName("Enter")
MsgBox "IsLabel_yes=" IsLabel("dc_label")
MsgBox "IsLabel_no=" IsLabel("nope")
dc_label:
````

## Upstream reference example

Source: [docs-v2/docs/lib/GetKeyName.htm](../../docs-v2/docs/lib/GetKeyName.htm)

````ahk
MsgBox GetKeyName("Esc") ; Shows Escape
MsgBox GetKeyName("vk1B") ; Shows also Escape
````
