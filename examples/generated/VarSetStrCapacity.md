# VarSetStrCapacity

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:157](../../tests/doccheck/assert_misc_cov.ahk#L157)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Enlarges a variable's holding capacity or frees its memory. This is not normally needed, but may be used with DllCall or SendMessage or to optimize repeated concatenation.

## Syntax

````text
GrantedCapacity := VarSetStrCapacity(&TargetVar , RequestedCapacity)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
VarCap() {
    s := "abc"
    c := VarSetStrCapacity(&s, 100)
    return c >= 100 ? "ok" : c
}
Check("varsetstrcap", VarCap)
````

## Upstream reference example

Source: [docs-v2/docs/lib/VarSetStrCapacity.htm](../../docs-v2/docs/lib/VarSetStrCapacity.htm)

````ahk
VarSetStrCapacity(&MyVar, 5120000)  ; ~10 MB
Loop
{
    ; ...
    MyVar .= StringToConcatenate
    ; ...
}
````
