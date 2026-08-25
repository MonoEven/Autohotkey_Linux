# HasMethod

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:154](../../tests/doccheck/assert_misc_cov.ahk#L154)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns a non-zero number if the specified value has a method by the specified name.

## Syntax

````text
HasMethod := HasMethod(Value , Name, ParamCount)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
}
Check("objsetbase", ObjBase)
Check("hasmethod", () => HasMethod({m: () => 1}, "m"))
VarCap() {
    s := "abc"
    c := VarSetStrCapacity(&s, 100)
````

## Upstream reference example

Source: [docs-v2/docs/lib/HasMethod.htm](../../docs-v2/docs/lib/HasMethod.htm)

````ahk
MsgBox HasMethod(0, "HasMethod") ; 1
MsgBox HasMethod(0, "Call") ; 0
````
