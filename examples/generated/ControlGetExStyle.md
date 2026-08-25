# ControlGetExStyle

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:207](../../tests/doccheck/assert_ctrl.ahk#L207)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: NotSupported for external windows (M5-B); process-local shadow only for own windows

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_setstyle=1")
try ControlGetExStyle("Button1", "CtlMain")
catch OSError
    Log("ns_exstyle=1")
try ControlSetExStyle("^0x100", "Button1", "CtlMain")
````
