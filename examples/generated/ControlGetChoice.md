# ControlGetChoice

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:240](../../tests/doccheck/assert_ctrl.ahk#L240)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the text of the currently selected entry in a list box, combo box, or drop-down list.

## Syntax

````text
Choice := ControlGetChoice(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_choosestring=1")
try ControlGetChoice("ComboBox1", "CtlMain")
catch OSError
    Log("ns_getchoice=1")
try ControlGetIndex("ComboBox1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetChoice.htm](../../docs-v2/docs/lib/ControlGetChoice.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
