# ControlHideDropDown

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:252](../../tests/doccheck/assert_ctrl.ahk#L252)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Hides the popup list of a combo box or drop-down list.

## Syntax

````text
ControlHideDropDown ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_showdd=1")
try ControlHideDropDown("ComboBox1", "CtlMain")
catch OSError
    Log("ns_hidedd=1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlHideDropDown.htm](../../docs-v2/docs/lib/ControlHideDropDown.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
