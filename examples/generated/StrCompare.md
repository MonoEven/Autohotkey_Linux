# StrCompare

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_string.ahk:29](../../tests/doccheck/assert_string.ahk#L29)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Compares two strings alphabetically.

## Syntax

````text
Result := StrCompare(String1, String2 , CaseSense)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "StrLower=" StrLower("ABC")
MsgBox "StrTitle=" StrTitle("hello world")
MsgBox "StrCompare_lt=" StrCompare("a", "b")
MsgBox "StrCompare_gt=" StrCompare("b", "a")
MsgBox "StrCompare_eq=" StrCompare("a", "a")
MsgBox "StrCompare_nocase=" StrCompare("A", "a", false)
````

## Upstream reference example

Source: [docs-v2/docs/lib/StrCompare.htm](../../docs-v2/docs/lib/StrCompare.htm)

````ahk
MsgBox StrCompare("Abc", "abc") ; Returns 0
MsgBox StrCompare("Abc", "abc", true) ; Returns -1
````
