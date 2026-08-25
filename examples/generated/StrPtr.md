# StrPtr

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:161](../../tests/doccheck/assert_misc_cov.ahk#L161)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the current memory address of a string.

## Syntax

````text
Address := StrPtr(Value)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
}
Check("varsetstrcap", VarCap)
Check("strptr", () => StrPtr("hello") != 0)

; --- Process (own PID via ProcessExist() with no args) -------------------
Check("ownpid", () => ProcessExist() > 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/StrPtr.htm](../../docs-v2/docs/lib/StrPtr.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
