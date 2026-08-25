# Pause

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:203](../../tests/doccheck/assert_misc_cov.ahk#L203)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Pauses the script's current thread or sets the pause state of the underlying thread.

## Syntax

````text
Pause UnderlyingThreadState
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- Output / state no-ops ----------------------------------------------
Check("outputdebug", () => (OutputDebug("misc-cov probe"), 1))
Check("pause_false", () => (Pause(false), 1))
Check("suspend_toggle", () => (Suspend(true), Suspend(false), 1))

; --- HotIf family (criterion callbacks take 1 param) ---------------------
````

## Upstream reference example

Source: [docs-v2/docs/lib/Pause.htm](../../docs-v2/docs/lib/Pause.htm)

````ahk
ListVars
Pause
ExitApp ; This line will not execute until the user unpauses the script.
````
