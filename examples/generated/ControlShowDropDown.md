# ControlShowDropDown

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:249](../../tests/doccheck/assert_ctrl.ahk#L249)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Shows the popup list of a combo box or drop-down list.

## Syntax

````text
ControlShowDropDown ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_getitems=1")
try ControlShowDropDown("ComboBox1", "CtlMain")
catch OSError
    Log("ns_showdd=1")
try ControlHideDropDown("ComboBox1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlShowDropDown.htm](../../docs-v2/docs/lib/ControlShowDropDown.htm)

````ahk
Send "#r"  ; Open the Run dialog.
WinWaitActive "ahk_class #32770"  ; Wait for the dialog to appear.
ControlShowDropDown "ComboBox1"  ; Show the popup list. The second parameter is omitted so that the last found window is used.
Sleep 2000
ControlHideDropDown "ComboBox1"  ; Hide the popup lis
````
