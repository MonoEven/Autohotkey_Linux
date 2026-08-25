# StrTitle

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_string.ahk:28](../../tests/doccheck/assert_string.ahk#L28)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "StrUpper=" StrUpper("abc")
MsgBox "StrLower=" StrLower("ABC")
MsgBox "StrTitle=" StrTitle("hello world")
MsgBox "StrCompare_lt=" StrCompare("a", "b")
MsgBox "StrCompare_gt=" StrCompare("b", "a")
MsgBox "StrCompare_eq=" StrCompare("a", "a")
````
