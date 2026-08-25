# Suspend

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:204](../../tests/doccheck/assert_misc_cov.ahk#L204)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Disables or enables all or selected hotkeys and hotstrings.

## Syntax

````text
Suspend NewState
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("outputdebug", () => (OutputDebug("misc-cov probe"), 1))
Check("pause_false", () => (Pause(false), 1))
Check("suspend_toggle", () => (Suspend(true), Suspend(false), 1))

; --- HotIf family (criterion callbacks take 1 param) ---------------------
HotCrit(c) {
````

## Upstream reference example

Source: [docs-v2/docs/lib/Suspend.htm](../../docs-v2/docs/lib/Suspend.htm)

````ahk
#SuspendExempt
^!s::Suspend  ; Ctrl+Alt+S
#SuspendExempt False
````
