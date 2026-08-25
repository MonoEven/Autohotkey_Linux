# Critical

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:103](../../tests/doccheck/assert_general.ahk#L103)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Prevents the current thread from being interrupted by other threads, or enables it to be interrupted.

## Syntax

````text
PrevSetting := Critical(Setting)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; ListLines / Critical / Thread (state functions)
MsgBox "ListLines_ret=" (ListLines() >= 0)
MsgBox "Critical_ret=" (Critical() >= 0)
MsgBox "Thread_ret=" (Thread("NoTimers") = 0)

; Sort / VerCompare / Type already covered elsewhere; spot checks:
````

## Upstream reference example

Source: [docs-v2/docs/lib/Critical.htm](../../docs-v2/docs/lib/Critical.htm)

````ahk
#space::  ; Win+Space hotkey.
{
    Critical
    ToolTip "No new threads will launch until after this ToolTip disappears."
    Sleep 3000
    ToolTip  ; Turn off the tip.
    return ; Returning from a hotkey function ends the thread. Any underlying thread to be resumed is noncritical by
````
