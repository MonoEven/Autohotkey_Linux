# EditGetSelectedText

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_edit.ahk:75](../../tests/doccheck/assert_edit.ahk#L75)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the selected text in an edit control.

## Syntax

````text
SelectedText := EditGetSelectedText(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- EditGetSelectedText: nothing is ever selected on the port (no way to
; create a selection without a real edit widget) -> empty string ---
Log("ed_sel_none=" (EditGetSelectedText("Edit1", "EdMain") = "" ? 1 : 0))

; --- ValueError: line index out of range (docs EditGetLine) ---
try {
````

## Upstream reference example

Source: [docs-v2/docs/lib/EditGetSelectedText.htm](../../docs-v2/docs/lib/EditGetSelectedText.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
