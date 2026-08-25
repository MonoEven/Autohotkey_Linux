# ProcessSetPriority

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:167](../../tests/doccheck/assert_misc_cov.ahk#L167)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Changes the priority level of the first matching process.

## Syntax

````text
PID := ProcessSetPriority(Level , PIDOrName)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("pwait", () => ProcessWait(ProcessExist(), 1) > 0)
Check("pwaitclose", () => ProcessWaitClose(ProcessExist(), 0.05) > 0)
Check("psetprio_omit", () => ProcessSetPriority("Normal") > 0)
Check("psetprio_own", () => ProcessSetPriority("Normal", ProcessExist()) > 0)
PersistFn() {
    Persistent()
````

## Upstream reference example

Source: [docs-v2/docs/lib/ProcessSetPriority.htm](../../docs-v2/docs/lib/ProcessSetPriority.htm)

````ahk
Run "notepad.exe", , , &NewPID
ProcessSetPriority "High", NewPID
MsgBox "The newly launched Notepad's PID is " NewPID
````
