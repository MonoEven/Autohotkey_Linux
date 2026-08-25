# ProcessGetName

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:153](../../tests/doccheck/assert_sys.ahk#L153)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Returns the name or path of the specified process.

## Syntax

````text
Name := ProcessGetName(PIDOrName) Path := ProcessGetPath(PIDOrName)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; ============================================================================

MsgBox "ProcName_self=" (ProcessGetName() = "ahk_core")
MsgBox "ProcPath_suffix=" (SubStr(ProcessGetPath(), -8) = "ahk_core")
pid := ProcessExist()
MsgBox "ProcName_bypid=" (ProcessGetName(pid) = "ahk_core")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ProcessGetName.htm](../../docs-v2/docs/lib/ProcessGetName.htm)

````ahk
Run "license.rtf",,, &pid  ; This is likely to exist in C:\Windows\System32.
try {
    name := ProcessGetName(pid)
    path := ProcessGetPath(pid)
}
MsgBox "Name: " (name ?? "could not be retrieved") "`n"
    .  "Path: " (path ?? "could not be retrieved")
````
