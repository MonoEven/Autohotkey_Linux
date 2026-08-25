# ControlGetEnabled

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:213](../../tests/doccheck/assert_ctrl.ahk#L213)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: NotSupported for external windows (M5-B); process-local shadow only for own windows

Returns 1 if a control is enabled, or 0 if disabled.

## Syntax

````text
IsEnabled := ControlGetEnabled(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_setexstyle=1")
try ControlGetEnabled("Button1", "CtlMain")
catch OSError
    Log("ns_enabled=1")
try ControlSetEnabled(0, "Button1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetEnabled.htm](../../docs-v2/docs/lib/ControlGetEnabled.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
