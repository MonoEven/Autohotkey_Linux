# RegExReplace

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_regex.ahk:16](../../tests/doccheck/assert_regex.ahk#L16)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Replaces occurrences of a pattern (regular expression) inside a string.

## Syntax

````text
NewStr := RegExReplace(Haystack, NeedleRegEx , Replacement, &OutputVarCount, Limit, StartingPos)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; RegExReplace: default replacement is "".
MsgBox "rep=" (RegExReplace("abc123", "\d+", "X") = "abcX")
MsgBox "rep_none=" (RegExReplace("abc", "xyz", "X") = "abc")
MsgBox "rep_ref=" (RegExReplace("abc", "(b)", "$1$1") = "abbc")
MsgBox "rep_case=" (RegExReplace("ABC", "i)abc", "x") = "x")
````

## Upstream reference example

Source: [docs-v2/docs/lib/RegExReplace.htm](../../docs-v2/docs/lib/RegExReplace.htm)

````ahk
MsgBox RegExReplace("abc123123", "123$", "xyz")
````
