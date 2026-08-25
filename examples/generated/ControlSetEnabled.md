# ControlSetEnabled

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:216](../../tests/doccheck/assert_ctrl.ahk#L216)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: NotSupported for external windows (M5-B); process-local shadow only for own windows

Enables or disables a control.

## Syntax

````text
ControlSetEnabled NewSetting, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_enabled=1")
try ControlSetEnabled(0, "Button1", "CtlMain")
catch OSError
    Log("ns_setenabled=1")
try ControlGetChecked("Button1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlSetEnabled.htm](../../docs-v2/docs/lib/ControlSetEnabled.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
