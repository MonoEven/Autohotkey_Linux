# ControlGetChecked

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:219](../../tests/doccheck/assert_ctrl.ahk#L219)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: NotSupported for external windows (M5-B); process-local shadow only for own windows

Returns 1 if a check box or radio button is checked, or 0 if unchecked.

## Syntax

````text
IsChecked := ControlGetChecked(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_setenabled=1")
try ControlGetChecked("Button1", "CtlMain")
catch OSError
    Log("ns_checked=1")
try ControlSetChecked(1, "Button1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetChecked.htm](../../docs-v2/docs/lib/ControlGetChecked.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
