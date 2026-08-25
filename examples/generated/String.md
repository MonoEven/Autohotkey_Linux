# String

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [examples/language/runtime_types.ahk:12](../../examples/language/runtime_types.ahk#L12)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Converts a value to a string.

## Syntax

````text
StrValue := String(Value)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
enumObject(&first)
functionObject := StrLen
text := String(42)

class ExampleRecord {
    __New(name) {
````

## Upstream reference example

Source: [docs-v2/docs/lib/String.htm](../../docs-v2/docs/lib/String.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
