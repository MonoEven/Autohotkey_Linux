# ControlAddItem

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:225](../../tests/doccheck/assert_ctrl.ahk#L225)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_edit.ahk:131](../../tests/doccheck/assert_edit.ahk#L131)

Adds a new entry at the bottom of a list box, combo box, or drop-down list.

## Syntax

````text
Index := ControlAddItem(String, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_setchecked=1")
try ControlAddItem("A", "ComboBox1", "CtlMain")
catch OSError
    Log("ns_additem=1")
try ControlDeleteItem(1, "ComboBox1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlAddItem.htm](../../docs-v2/docs/lib/ControlAddItem.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
