# Enumerator

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [examples/language/runtime_types.ahk:8](../../examples/language/runtime_types.ahk#L8)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

An enumerator is a type of function object which is called repeatedly to enumerate a sequence of values.

## Syntax

````text
Boolean := Enum.Call(&OutputVar1 , &OutputVar2)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

values := Array(10, 20, 30)
enumObject := values.__Enum(1)
first := 0
enumObject(&first)
functionObject := StrLen
````

## Upstream reference example

Source: [docs-v2/docs/lib/Enumerator.htm](../../docs-v2/docs/lib/Enumerator.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
