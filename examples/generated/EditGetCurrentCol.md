# EditGetCurrentCol

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_edit.ahk:55](../../tests/doccheck/assert_edit.ahk#L55)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the column number in an edit control where the caret resides.

## Syntax

````text
CurrentCol := EditGetCurrentCol(ControlID , WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- M5-B: the virtual caret position (EM_GETSEL) is NotSupported on
; --- external windows. ---
try EditGetCurrentCol("Edit1", "EdMain")
catch OSError
    Log("ns_col=1")
try EditGetCurrentLine("Edit1", "EdMain")
````

## Upstream reference example

Source: [docs-v2/docs/lib/EditGetCurrentCol.htm](../../docs-v2/docs/lib/EditGetCurrentCol.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
