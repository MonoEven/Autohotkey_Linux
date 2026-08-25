# NumPut

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_interop.ahk:7](../../tests/doccheck/assert_interop.ahk#L7)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_registry.ahk:75](../../tests/doccheck/assert_registry.ahk#L75)

Stores one or more numbers in binary format at the specified address+offset.

## Syntax

````text
NumPut Type, Number, Type2, Number2, ... Target , Offset
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

b := Buffer(32)
NumPut("Int", 42, b, 0)
MsgBox "NumGet_int=" (NumGet(b, 0, "Int") = 42)
MsgBox "NumGet_2param=" (NumGet(b, "Int") = 42)
NumPut("Char", -7, b, 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/NumPut.htm](../../docs-v2/docs/lib/NumPut.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
