# ProcessWait

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:165](../../tests/doccheck/assert_misc_cov.ahk#L165)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Waits for the specified process to exist.

## Syntax

````text
PID := ProcessWait(PIDOrName , Timeout)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- Process (own PID via ProcessExist() with no args) -------------------
Check("ownpid", () => ProcessExist() > 0)
Check("pwait", () => ProcessWait(ProcessExist(), 1) > 0)
Check("pwaitclose", () => ProcessWaitClose(ProcessExist(), 0.05) > 0)
Check("psetprio_omit", () => ProcessSetPriority("Normal") > 0)
Check("psetprio_own", () => ProcessSetPriority("Normal", ProcessExist()) > 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/ProcessWait.htm](../../docs-v2/docs/lib/ProcessWait.htm)

````ahk
NewPID := ProcessWait("notepad.exe", 5.5)
if not NewPID
{
    MsgBox "The specified process did not appear within 5.5 seconds."
    return
}
; Otherwise:
MsgBox "A matching process has appeared (Process ID is " NewPID ")."
ProcessSetPriority "Low", NewPID
ProcessSetPriority "High"
````
