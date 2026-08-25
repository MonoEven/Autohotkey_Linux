# EditPaste

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_edit.ahk:64](../../tests/doccheck/assert_edit.ahk#L64)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Pastes a string at the caret in an edit control.

## Syntax

````text
EditPaste String, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- EditPaste: the text insertion is REAL (WM_SETTEXT-style write); the
; --- caret after the paste is virtual and refuses to be reported. ---
EditPaste("X", "Edit1", "EdMain")
Log("ed_paste_text=" (ControlGetText("Edit1", "EdMain") = "Xalpha`nbeta`ngamma" ? 1 : 0))
EditPaste("one`ntwo", "Edit1", "EdMain")
Log("ed_paste2_text=" (ControlGetText("Edit1", "EdMain") = "Xone`ntwoalpha`nbeta`ngamma" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/EditPaste.htm](../../docs-v2/docs/lib/EditPaste.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
