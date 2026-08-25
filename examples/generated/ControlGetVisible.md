# ControlGetVisible

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:257](../../tests/doccheck/assert_ctrl.ahk#L257)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns 1 if a control is visible, or 0 if hidden.

## Syntax

````text
IsVisible := ControlGetVisible(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- ControlGetVisible / ControlHide / ControlShow (REAL X11 operations). ---
Log("visible0=" (ControlGetVisible("Hidden1", "CtlMain") = 1 ? 1 : 0))
ControlHide("Hidden1", "CtlMain")
Log("hidden=" (ControlGetVisible("Hidden1", "CtlMain") = 0 ? 1 : 0))
ControlShow("Hidden1", "CtlMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetVisible.htm](../../docs-v2/docs/lib/ControlGetVisible.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
