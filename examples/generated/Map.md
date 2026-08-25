# Map

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:72](../../tests/doccheck/assert_ctrl.ahk#L72)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_object.ahk:33](../../tests/doccheck/assert_object.ahk#L33)
- `headless`: [tests/doccheck/assert_statements.ahk:95](../../tests/doccheck/assert_statements.ahk#L95)

## Syntax

````text
MapObj := Map(Key1, Value1, Key2, Value2, ...) MapObj := Map.Call(Key1, Value1, Key2, Value2, ...)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    parts := StrSplit(out, ",")
    out := ""
    seen := Map()
    for p in parts {
        if p != "" && !seen.Has(p) {
            seen[p] := 1
````

## Upstream reference example

Source: [docs-v2/docs/lib/Map.htm](../../docs-v2/docs/lib/Map.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
