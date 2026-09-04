# ProcessExist

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:34](../../tests/doccheck/assert_general.ahk#L34)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_sys.ahk:155](../../tests/doccheck/assert_sys.ahk#L155)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:55](../../tests/doccheck/assert_unicode_lease.ahk#L55)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:164](../../tests/doccheck/assert_misc_cov.ahk#L164)
- `x11`: [tests/doccheck/assert_clipboard_all.ahk:60](../../tests/doccheck/assert_clipboard_all.ahk#L60)

Checks if the specified process exists.

## Syntax

````text
PID := ProcessExist(PIDOrName)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Process module
MsgBox "ProcessExist_self=" (ProcessExist() > 0)
Run "sleep 5", , , &pid
MsgBox "Run_pid=" (pid > 0)
MsgBox "ProcessExist_pid=" (ProcessExist(pid) = pid)
````

## Upstream reference example

Source: [docs-v2/docs/lib/ProcessExist.htm](../../docs-v2/docs/lib/ProcessExist.htm)

````ahk
if (PID := ProcessExist("notepad.exe"))
    MsgBox "Notepad exists and has the Process ID " PID "."
else
    MsgBox "Notepad does not exist."
````
