# Object

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_statements.ahk:99](../../tests/doccheck/assert_statements.ahk#L99)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_misc_cov.ahk:134](../../tests/doccheck/assert_misc_cov.ahk#L134)

## Syntax

````text
Obj := Object() Obj := Object.Call()
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
}
ObjectMethods() {
    o := Object()
    o.x := 10
    return o.x + (o.HasOwnProp("x") ? 1 : 0)
}
````

## Upstream reference example

Source: [docs-v2/docs/lib/Object.htm](../../docs-v2/docs/lib/Object.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
