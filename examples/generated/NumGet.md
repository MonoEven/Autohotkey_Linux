# NumGet

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_interop.ahk:8](../../tests/doccheck/assert_interop.ahk#L8)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Returns the binary number stored at the specified address+offset.

## Syntax

````text
Number := NumGet(Source, Offset, Type) Number := NumGet(Source, Type)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
b := Buffer(32)
NumPut("Int", 42, b, 0)
MsgBox "NumGet_int=" (NumGet(b, 0, "Int") = 42)
MsgBox "NumGet_2param=" (NumGet(b, "Int") = 42)
NumPut("Char", -7, b, 0)
MsgBox "NumGet_char=" (NumGet(b, 0, "Char") = -7)
````

## Upstream reference example

Source: [docs-v2/docs/lib/NumGet.htm](../../docs-v2/docs/lib/NumGet.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
