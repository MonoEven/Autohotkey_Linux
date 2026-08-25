# ObjBindMethod

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:75](../../tests/doccheck/assert_misc_cov.ahk#L75)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Creates a BoundFunc object which calls a method of a given object.

## Syntax

````text
BoundFunc := ObjBindMethod(Obj , Method, Params)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
BindClsFn() {
    o := BindClsM()
    f := ObjBindMethod(o, "m")
    return f.Call()
}
Check("objbind_method", BindClsFn)
````

## Upstream reference example

Source: [docs-v2/docs/lib/ObjBindMethod.htm](../../docs-v2/docs/lib/ObjBindMethod.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
