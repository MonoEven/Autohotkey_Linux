# ProcessClose

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:38](../../tests/doccheck/assert_general.ahk#L38)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Forces the first matching process to close.

## Syntax

````text
PID := ProcessClose(PIDOrName)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Run_pid=" (pid > 0)
MsgBox "ProcessExist_pid=" (ProcessExist(pid) = pid)
ProcessClose(pid)
; A loaded CI runner can take a while to reap the terminated process; poll
; (bounded) instead of a fixed short sleep so the assertion never flakes.
reaped := 0
````

## Upstream reference example

Source: [docs-v2/docs/lib/ProcessClose.htm](../../docs-v2/docs/lib/ProcessClose.htm)

````ahk
ProcessClose "notepad.exe"
````
