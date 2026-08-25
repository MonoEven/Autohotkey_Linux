# LTrim

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_string.ahk:24](../../tests/doccheck/assert_string.ahk#L24)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "StrReplace_none=" StrReplace("abc", "z", "X")
MsgBox "Trim=" Trim("  x  ")
MsgBox "LTrim=" LTrim("  x  ")
MsgBox "RTrim=" RTrim("  x  ")
MsgBox "StrUpper=" StrUpper("abc")
MsgBox "StrLower=" StrLower("ABC")
````
