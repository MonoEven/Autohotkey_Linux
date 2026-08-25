# Class

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [examples/language/runtime_types.ahk:14](../../examples/language/runtime_types.ahk#L14)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Syntax

````text
Obj := ClassObj(Params*) Obj := ClassObj.Call(Params*)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
text := String(42)

class ExampleRecord {
    __New(name) {
        this.Name := name
    }
````

## Upstream reference example

Source: [docs-v2/docs/lib/Class.htm](../../docs-v2/docs/lib/Class.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
