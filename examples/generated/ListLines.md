# ListLines

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:102](../../tests/doccheck/assert_general.ahk#L102)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Enables or disables line logging or displays the script lines most recently executed.

## Syntax

````text
PrevSetting := ListLines(Setting)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; ListLines / Critical / Thread (state functions)
MsgBox "ListLines_ret=" (ListLines() >= 0)
MsgBox "Critical_ret=" (Critical() >= 0)
MsgBox "Thread_ret=" (Thread("NoTimers") = 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/ListLines.htm](../../docs-v2/docs/lib/ListLines.htm)

````ahk
x := "This line is logged"
ListLines False
x := "This line is not logged"
ListLines True
ListLines
MsgBox
````
