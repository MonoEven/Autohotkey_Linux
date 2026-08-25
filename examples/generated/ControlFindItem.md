# ControlFindItem

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:231](../../tests/doccheck/assert_ctrl.ahk#L231)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Searches for an entry in a list box, combo box, or drop-down list by string, and returns its index.

## Syntax

````text
Index := ControlFindItem(String, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_deleteitem=1")
try ControlFindItem("A", "ComboBox1", "CtlMain")
catch OSError
    Log("ns_finditem=1")
try ControlChooseIndex(1, "ComboBox1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlFindItem.htm](../../docs-v2/docs/lib/ControlFindItem.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
