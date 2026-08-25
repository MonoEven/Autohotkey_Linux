# SetWorkingDir

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:13](../../tests/doccheck/assert_general.ahk#L13)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Changes the script's current working directory.

## Syntax

````text
SetWorkingDir DirName
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; Working dir
old := A_WorkingDir
SetWorkingDir("/tmp")
MsgBox "SetWorkingDir=" (A_WorkingDir = "/tmp")
SetWorkingDir(old)
MsgBox "SetWorkingDir_restore=" (A_WorkingDir = old)
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetWorkingDir.htm](../../docs-v2/docs/lib/SetWorkingDir.htm)

````ahk
SetWorkingDir "D:\My Folder\Temp"
````
