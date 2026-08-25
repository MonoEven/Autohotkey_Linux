# ControlShow

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:260](../../tests/doccheck/assert_ctrl.ahk#L260)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Shows a control if it was previously hidden.

## Syntax

````text
ControlShow ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ControlHide("Hidden1", "CtlMain")
Log("hidden=" (ControlGetVisible("Hidden1", "CtlMain") = 0 ? 1 : 0))
ControlShow("Hidden1", "CtlMain")
Log("shown=" (ControlGetVisible("Hidden1", "CtlMain") = 1 ? 1 : 0))

; --- Error paths (docs: TargetError for window/control not found; the class
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlShow.htm](../../docs-v2/docs/lib/ControlShow.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
