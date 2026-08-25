# FileGetTime

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_file.ahk:49](../../tests/doccheck/assert_file.ahk#L49)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Retrieves the datetime stamp of a file or folder.

## Syntax

````text
Timestamp := FileGetTime(Filename, WhichTime)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: FileGetTime returns YYYYMMDDHH24MISS (14 digits), local time.
MsgBox "GetTime_len=" (StrLen(FileGetTime(fm)) = 14)
MsgBox "GetTime_A_len=" (StrLen(FileGetTime(fm, "A")) = 14)
MsgBox "GetTime_C_len=" (StrLen(FileGetTime(fm, "C")) = 14)
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileGetTime.htm](../../docs-v2/docs/lib/FileGetTime.htm)

````ahk
Timestamp := FileGetTime("C:\My Documents\test.doc")
````
