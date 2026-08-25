# ControlGetText

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:95](../../tests/doccheck/assert_ctrl.ahk#L95)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_edit.ahk:65](../../tests/doccheck/assert_edit.ahk#L65)

Retrieves text from a control.

## Syntax

````text
Text := ControlGetText(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- ControlGetText / ControlSetText (ClassNN, HWND and text identifiers). ---
Log("gettext_classnn=" (ControlGetText("Edit1", "CtlMain") = "Edit1" ? 1 : 0))
; check_detail0824 M0-B: GNOME desktop name and a leaked WAYLAND_DISPLAY must
; not override an explicit XDG_SESSION_TYPE=x11 session.
old_st := EnvGet("XDG_SESSION_TYPE")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlGetText.htm](../../docs-v2/docs/lib/ControlGetText.htm)

````ahk
Text := ControlGetText("Edit1", "Untitled -")
````
