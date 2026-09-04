# ProcessWaitClose

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:166](../../tests/doccheck/assert_misc_cov.ahk#L166)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_clipboard_all.ahk:164](../../tests/doccheck/assert_clipboard_all.ahk#L164)

Waits for all matching processes to close.

## Syntax

````text
PID := ProcessWaitClose(PIDOrName , Timeout)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("ownpid", () => ProcessExist() > 0)
Check("pwait", () => ProcessWait(ProcessExist(), 1) > 0)
Check("pwaitclose", () => ProcessWaitClose(ProcessExist(), 0.05) > 0)
Check("psetprio_omit", () => ProcessSetPriority("Normal") > 0)
Check("psetprio_own", () => ProcessSetPriority("Normal", ProcessExist()) > 0)
PersistFn() {
````

## Upstream reference example

Source: [docs-v2/docs/lib/ProcessWaitClose.htm](../../docs-v2/docs/lib/ProcessWaitClose.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
