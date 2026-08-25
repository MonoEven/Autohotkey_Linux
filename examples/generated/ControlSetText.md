# ControlSetText

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:108](../../tests/doccheck/assert_ctrl.ahk#L108)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_edit.ahk:37](../../tests/doccheck/assert_edit.ahk#L37)
- `x11`: [tests/doccheck/assert_monitor.ahk:69](../../tests/doccheck/assert_monitor.ahk#L69)

Changes the text of a control.

## Syntax

````text
ControlSetText NewText, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
EnvSet("XDG_CURRENT_DESKTOP", old_desktop)
EnvSet("WAYLAND_DISPLAY", old_wl)
ControlSetText("Hello World", "Edit1", "CtlMain")
Log("settext=" (ControlGetText("Edit1", "CtlMain") = "Hello World" ? 1 : 0))
ehwnd := ControlGetHwnd("Edit1", "CtlMain")
Log("gettext_hwnd=" (ControlGetText(ehwnd) = "Hello World" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlSetText.htm](../../docs-v2/docs/lib/ControlSetText.htm)

````ahk
ControlSetText("New Text Here", "Edit1", "Untitled -")
````
