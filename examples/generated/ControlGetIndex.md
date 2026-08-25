# ControlGetIndex

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:243](../../tests/doccheck/assert_ctrl.ahk#L243)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the index of the currently selected entry in a list box, combo box, or drop-down list, or the index of the active page in a tab control.

## Syntax

````text
Index := ControlGetIndex(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_getchoice=1")
try ControlGetIndex("ComboBox1", "CtlMain")
catch OSError
    Log("ns_getindex=1")
try ControlGetItems("ComboBox1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetIndex.htm](../../docs-v2/docs/lib/ControlGetIndex.htm)

````ahk
WhichTab := ControlGetIndex("SysTabControl321", "Some Window Title")
MsgBox "Tab #" WhichTab " is active."
````
