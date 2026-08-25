# Number

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_statements.ahk:115](../../tests/doccheck/assert_statements.ahk#L115)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Converts a numeric string to a pure integer or floating-point number.

## Syntax

````text
NumValue := Number(Value)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
}
NumClass() {
    return Number("3.5") + 0.5
}
RoundMath() {
    return Round(3.7)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Number.htm](../../docs-v2/docs/lib/Number.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
