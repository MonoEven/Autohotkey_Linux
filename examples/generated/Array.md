# Array

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [examples/language/runtime_types.ahk:7](../../examples/language/runtime_types.ahk#L7)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Syntax

````text
ArrayObj := Array(Value, Value2, ..., ValueN) ArrayObj := Array.Call(Value, Value2, ..., ValueN)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    FileDelete(out)

values := Array(10, 20, 30)
enumObject := values.__Enum(1)
first := 0
enumObject(&first)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Array.htm](../../docs-v2/docs/lib/Array.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
