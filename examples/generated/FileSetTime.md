# FileSetTime

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_file.ahk:54](../../tests/doccheck/assert_file.ahk#L54)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Changes the  datetime stamp of one or more files or folders. Wildcards are supported.

## Syntax

````text
FileSetTime YYYYMMDDHH24MISS, FilePattern, WhichTime, Mode
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: FileSetTime sets the timestamp given as YYYYMMDDHH24MISS (local).
FileSetTime("20200102030405", fm)
MsgBox "SetTime_M=" (FileGetTime(fm) = "20200102030405")

; Docs: FileGetSize units B (default) / K / M, rounded down.
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileSetTime.htm](../../docs-v2/docs/lib/FileSetTime.htm)

````ahk
FileSetTime "", "C:\temp\*.txt"
````
