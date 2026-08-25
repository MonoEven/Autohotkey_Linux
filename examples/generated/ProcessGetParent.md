# ProcessGetParent

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:157](../../tests/doccheck/assert_sys.ahk#L157)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Returns the process ID (PID) of the process which created the specified process.

## Syntax

````text
PID := ProcessGetParent(PIDOrName)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
pid := ProcessExist()
MsgBox "ProcName_bypid=" (ProcessGetName(pid) = "ahk_core")
MsgBox "ProcParent_alive=" (ProcessGetParent() > 0 && ProcessGetName(ProcessGetParent()) != "")
MsgBox "ProcName_byname=" (ProcessGetName("ahk_core") = "ahk_core")
; Docs: "The name is not case-sensitive".
MsgBox "ProcName_case=" (ProcessGetName("AHK_CORE") = "ahk_core")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ProcessGetParent.htm](../../docs-v2/docs/lib/ProcessGetParent.htm)

````ahk
try
    MsgBox ProcessGetName(ProcessGetParent())
catch
    MsgBox "Unable to retrieve parent process name; the process has likely exited."
````
