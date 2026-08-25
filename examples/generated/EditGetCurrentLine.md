# EditGetCurrentLine

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_edit.ahk:58](../../tests/doccheck/assert_edit.ahk#L58)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the line number in an edit control where the caret resides.

## Syntax

````text
CurrentLine := EditGetCurrentLine(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch OSError
    Log("ns_col=1")
try EditGetCurrentLine("Edit1", "EdMain")
catch OSError
    Log("ns_row=1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/EditGetCurrentLine.htm](../../docs-v2/docs/lib/EditGetCurrentLine.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
