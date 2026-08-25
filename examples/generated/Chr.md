# Chr

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_string.ahk:44](../../tests/doccheck/assert_string.ahk#L44)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Returns the string (usually a single character) corresponding to the character code indicated by the specified number.

## Syntax

````text
String := Chr(Number)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Ord=" Ord("A")
MsgBox "Ord_empty=" Ord("")
MsgBox "Chr=" Chr(65)
MsgBox "Chr_0=" Chr(0)
MsgBox "Sort_comma=" Sort("c,b,a", "D,")
MsgBox "Sort_numeric=" Sort("3,1,2", "N D,")
````

## Upstream reference example

Source: [docs-v2/docs/lib/Chr.htm](../../docs-v2/docs/lib/Chr.htm)

````ahk
MsgBox Chr(116) ; Reports "t".
````
