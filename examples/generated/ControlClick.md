# ControlClick

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:149](../../tests/doccheck/assert_ctrl.ahk#L149)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Sends a mouse button or mouse wheel event to a window or control.

## Syntax

````text
ControlClick ControlID-or-Pos, WinTitle, WinText, WhichButton, ClickCount, Options, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- ControlClick (ClassNN, position mode, buttons, count, D/U options). ---
ControlClick("Button1", "CtlMain")
Sleep(80)
cl_lines := next_lines()
Log("click=" (btns(cl_lines) = "down:1,up:1" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlClick.htm](../../docs-v2/docs/lib/ControlClick.htm)

````ahk
ControlClick "OK", "Some Window Title"
````
