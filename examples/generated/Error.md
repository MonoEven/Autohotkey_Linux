# Error

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_statements.ahk:108](../../tests/doccheck/assert_statements.ahk#L108)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Syntax

````text
ErrorObj := Error(Message , What, Extra) ErrorObj := Error.Call(Message , What, Extra)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
}
ErrorClass() {
    e := Error("msg", "what")
    return e is Error ? "ok" : "bad"
}
StrConcat() {
````

## Upstream reference example

Source: [docs-v2/docs/lib/Error.htm](../../docs-v2/docs/lib/Error.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
