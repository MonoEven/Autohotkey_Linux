# IsSet

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:81](../../tests/doccheck/assert_misc_cov.ahk#L81)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns a non-zero number if the specified variable has been assigned a value.

## Syntax

````text
Boolean := IsSet(Var) Boolean := IsSetRef(&Ref)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- IsSet / IsSetRef ----------------------------------------------------
Check("isset_unset", () => IsSet(probe_never_assigned))
Check("isset_set", () => (probe_assigned := 1, IsSet(probe_assigned)))
RefCheck(&p) {
    return IsSetRef(&p)
````

## Upstream reference example

Source: [docs-v2/docs/lib/IsSet.htm](../../docs-v2/docs/lib/IsSet.htm)

````ahk
Loop 2
    if !IsSet(MyVar)  ; Is this the first "use" of MyVar?
        MyVar := A_Index  ; Initialize on first "use".
MsgBox Function1(&MyVar)
MsgBox Function2(&MyVar)
Function1(&Param)  ; ByRef parameter.
{
    if IsSet(Param)  ; Pass Param itself, which is an alias for MyVar.
````
