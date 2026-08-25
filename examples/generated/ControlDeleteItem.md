# ControlDeleteItem

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:228](../../tests/doccheck/assert_ctrl.ahk#L228)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_edit.ahk:134](../../tests/doccheck/assert_edit.ahk#L134)

Deletes an entry from a list box, combo box, or drop-down list by index.

## Syntax

````text
ControlDeleteItem N, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_additem=1")
try ControlDeleteItem(1, "ComboBox1", "CtlMain")
catch OSError
    Log("ns_deleteitem=1")
try ControlFindItem("A", "ComboBox1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlDeleteItem.htm](../../docs-v2/docs/lib/ControlDeleteItem.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
