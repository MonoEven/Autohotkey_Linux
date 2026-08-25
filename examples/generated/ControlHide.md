# ControlHide

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:258](../../tests/doccheck/assert_ctrl.ahk#L258)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Hides a control.

## Syntax

````text
ControlHide ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- ControlGetVisible / ControlHide / ControlShow (REAL X11 operations). ---
Log("visible0=" (ControlGetVisible("Hidden1", "CtlMain") = 1 ? 1 : 0))
ControlHide("Hidden1", "CtlMain")
Log("hidden=" (ControlGetVisible("Hidden1", "CtlMain") = 0 ? 1 : 0))
ControlShow("Hidden1", "CtlMain")
Log("shown=" (ControlGetVisible("Hidden1", "CtlMain") = 1 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlHide.htm](../../docs-v2/docs/lib/ControlHide.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
