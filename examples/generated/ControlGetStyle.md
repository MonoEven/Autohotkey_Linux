# ControlGetStyle

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:201](../../tests/doccheck/assert_ctrl.ahk#L201)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: NotSupported for external windows (M5-B); process-local shadow only for own windows

Returns an integer representing the style or extended style of a control.

## Syntax

````text
Style := ControlGetStyle(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText) ExStyle := ControlGetExStyle(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- entries) has no real X11 effect on EXTERNAL windows; the port refuses
; --- to pretend success and throws OSError instead. ---
try ControlGetStyle("Button1", "CtlMain")
catch OSError
    Log("ns_style=1")
try ControlSetStyle("0x10", "Button1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetStyle.htm](../../docs-v2/docs/lib/ControlGetStyle.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
