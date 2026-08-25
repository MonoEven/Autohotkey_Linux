# ProcessGetPath

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:154](../../tests/doccheck/assert_sys.ahk#L154)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

MsgBox "ProcName_self=" (ProcessGetName() = "ahk_core")
MsgBox "ProcPath_suffix=" (SubStr(ProcessGetPath(), -8) = "ahk_core")
pid := ProcessExist()
MsgBox "ProcName_bypid=" (ProcessGetName(pid) = "ahk_core")
MsgBox "ProcParent_alive=" (ProcessGetParent() > 0 && ProcessGetName(ProcessGetParent()) != "")
````
