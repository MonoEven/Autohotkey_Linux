# EditGetLine

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_edit.ahk:39](../../tests/doccheck/assert_edit.ahk#L39)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the text of a line in an edit control by line number.

## Syntax

````text
Line := EditGetLine(N, ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ControlSetText("alpha`nbeta`ngamma", "Edit1", "EdMain")
Log("ed_lines=" (EditGetLineCount("Edit1", "EdMain") = 3 ? 1 : 0))
Log("ed_line1=" (EditGetLine(1, "Edit1", "EdMain") = "alpha" ? 1 : 0))
Log("ed_line2=" (EditGetLine(2, "Edit1", "EdMain") = "beta" ? 1 : 0))
Log("ed_line3=" (EditGetLine(3, "Edit1", "EdMain") = "gamma" ? 1 : 0))
; Empty control: the helper names children, so clear the text first.
````

## Upstream reference example

Source: [docs-v2/docs/lib/EditGetLine.htm](../../docs-v2/docs/lib/EditGetLine.htm)

````ahk
MsgBox EditGetLine(2, "Edit1", "Some Window Title")
````
