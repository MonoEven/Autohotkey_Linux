# OutputDebug

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:202](../../tests/doccheck/assert_misc_cov.ahk#L202)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Sends a string to the debugger (if any) for display.

## Syntax

````text
OutputDebug Text
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- Output / state no-ops ----------------------------------------------
Check("outputdebug", () => (OutputDebug("misc-cov probe"), 1))
Check("pause_false", () => (Pause(false), 1))
Check("suspend_toggle", () => (Suspend(true), Suspend(false), 1))
````

## Upstream reference example

Source: [docs-v2/docs/lib/OutputDebug.htm](../../docs-v2/docs/lib/OutputDebug.htm)

````ahk
OutputDebug A_Now ': Because the window "' TargetWindowTitle '" did not exist, the process was aborted.'
````
