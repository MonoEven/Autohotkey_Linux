# FileCopy

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:211](../../tests/doccheck/assert_sys.ahk#L211)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Copies one or more files.

## Syntax

````text
FileCopy SourcePattern, DestPattern , Overwrite
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
DirCreate(D)
FileAppend("hello", D "/src.txt")
FileCopy(D "/src.txt", D "/dst.txt")
MsgBox "Copy_basic=" (FileRead(D "/dst.txt") = "hello")
try {
    FileCopy(D "/src.txt", D "/dst.txt", 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileCopy.htm](../../docs-v2/docs/lib/FileCopy.htm)

````ahk
FileCopy "C:\My Documents\List1.txt", "D:\Main Backup\"
````
