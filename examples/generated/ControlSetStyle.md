# ControlSetStyle

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:204](../../tests/doccheck/assert_ctrl.ahk#L204)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: NotSupported for external windows (M5-B); process-local shadow only for own windows

Changes the style or extended style of a control.

## Syntax

````text
ControlSetStyle Value, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText ControlSetExStyle Value, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_style=1")
try ControlSetStyle("0x10", "Button1", "CtlMain")
catch OSError
    Log("ns_setstyle=1")
try ControlGetExStyle("Button1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlSetStyle.htm](../../docs-v2/docs/lib/ControlSetStyle.htm)

````ahk
ControlSetStyle("^0x800000", "Edit1", "ahk_class Notepad")
````
