# ControlChooseString

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:237](../../tests/doccheck/assert_ctrl.ahk#L237)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Selects an entry in a list box, combo box, or drop-down list, or a tab control page, by string.

## Syntax

````text
Index := ControlChooseString(String, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_chooseindex=1")
try ControlChooseString("A", "ComboBox1", "CtlMain")
catch OSError
    Log("ns_choosestring=1")
try ControlGetChoice("ComboBox1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlChooseString.htm](../../docs-v2/docs/lib/ControlChooseString.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
