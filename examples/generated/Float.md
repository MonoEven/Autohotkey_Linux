# Float

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:45](../../tests/doccheck/assert_misc_cov.ahk#L45)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Converts a numeric string or integer value to a floating-point number.

## Syntax

````text
FltValue := Float(Value)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- Primitive classes / function objects -------------------------------
Check("float_val", () => Float("2.5"))
Check("float_type", () => Type(Float("2.5")))
Check("int_val", () => Integer("7"))
Check("int_type", () => Type(Integer(7)))
````

## Upstream reference example

Source: [docs-v2/docs/lib/Float.htm](../../docs-v2/docs/lib/Float.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
