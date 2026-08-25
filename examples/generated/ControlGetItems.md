# ControlGetItems

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:246](../../tests/doccheck/assert_ctrl.ahk#L246)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns an array of entries from a list box, combo box, or drop-down list.

## Syntax

````text
Items := ControlGetItems(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_getindex=1")
try ControlGetItems("ComboBox1", "CtlMain")
catch OSError
    Log("ns_getitems=1")
try ControlShowDropDown("ComboBox1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetItems.htm](../../docs-v2/docs/lib/ControlGetItems.htm)

````ahk
for entry in ControlGetItems("ComboBox1", "Some Window Title")
    MsgBox "Entry number " A_Index " is " entry
````
