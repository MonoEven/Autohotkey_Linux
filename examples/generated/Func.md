# Func

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [examples/language/runtime_types.ahk:11](../../examples/language/runtime_types.ahk#L11)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Syntax

````text
FuncObj(Param1, Param2, ...) FuncObj.Call(Param1, Param2, ...)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
first := 0
enumObject(&first)
functionObject := StrLen
text := String(42)

class ExampleRecord {
````

## Upstream reference example

Source: [docs-v2/docs/lib/Func.htm](../../docs-v2/docs/lib/Func.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
