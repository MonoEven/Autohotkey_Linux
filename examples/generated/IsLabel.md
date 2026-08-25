# IsLabel

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:109](../../tests/doccheck/assert_general.ahk#L109)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Returns a non-zero number if the specified label exists in the current scope.

## Syntax

````text
Boolean := IsLabel(LabelName)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "VerCompare=" VerCompare("2.0.26", "2.0.20")
MsgBox "GetKeyName=" GetKeyName("Enter")
MsgBox "IsLabel_yes=" IsLabel("dc_label")
MsgBox "IsLabel_no=" IsLabel("nope")
dc_label:
````

## Upstream reference example

Source: [docs-v2/docs/lib/IsLabel.htm](../../docs-v2/docs/lib/IsLabel.htm)

````ahk
if IsLabel("Label")
    MsgBox "Target label exists"
else
    MsgBox "Target label doesn't exist"
Label:
return
````
