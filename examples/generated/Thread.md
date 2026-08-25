# Thread

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:104](../../tests/doccheck/assert_general.ahk#L104)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Sets the priority or interruptibility of threads. It can also temporarily disable all timers.

## Syntax

````text
Thread SubFunction , Value1, Value2
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "ListLines_ret=" (ListLines() >= 0)
MsgBox "Critical_ret=" (Critical() >= 0)
MsgBox "Thread_ret=" (Thread("NoTimers") = 0)

; Sort / VerCompare / Type already covered elsewhere; spot checks:
MsgBox "VerCompare=" VerCompare("2.0.26", "2.0.20")
````

## Upstream reference example

Source: [docs-v2/docs/lib/Thread.htm](../../docs-v2/docs/lib/Thread.htm)

````ahk
Thread "Priority", 1
````
