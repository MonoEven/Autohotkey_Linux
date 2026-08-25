# OnExit

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:42](../../tests/doccheck/assert_misc_cov.ahk#L42)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Registers a function to be called automatically whenever the script exits.

## Syntax

````text
OnExit Callback , AddRemove
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- OnExit handler (runs at ExitApp; appends onexit_ran). ---------------
OnExit((*) => (FileAppend("onexit_ran=1`n", OUT), 0))

; --- Primitive classes / function objects -------------------------------
Check("float_val", () => Float("2.5"))
````

## Upstream reference example

Source: [docs-v2/docs/lib/OnExit.htm](../../docs-v2/docs/lib/OnExit.htm)

````ahk
Persistent  ; Prevent the script from exiting automatically.
OnExit MyObject.Exiting
class MyObject
{
    static Exiting(*)
    {
        MsgBox "MyObject is cleaning up prior to exiting..."
        /*
        this.SayGoodbye()
        this.CloseNetworkConnections()
        */
````
