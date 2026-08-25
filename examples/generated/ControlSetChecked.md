# ControlSetChecked

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:222](../../tests/doccheck/assert_ctrl.ahk#L222)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: NotSupported for external windows (M5-B); process-local shadow only for own windows

Checks or unchecks a check box or radio button.

## Syntax

````text
ControlSetChecked NewSetting, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_checked=1")
try ControlSetChecked(1, "Button1", "CtlMain")
catch OSError
    Log("ns_setchecked=1")
try ControlAddItem("A", "ComboBox1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlSetChecked.htm](../../docs-v2/docs/lib/ControlSetChecked.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
