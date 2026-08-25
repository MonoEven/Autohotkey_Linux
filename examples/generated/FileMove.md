# FileMove

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:242](../../tests/doccheck/assert_sys.ahk#L242)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Moves or renames one or more files.

## Syntax

````text
FileMove SourcePattern, DestPattern , Overwrite
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

FileAppend("x", D "/m1.txt")
FileMove(D "/m1.txt", D "/m2.txt")
MsgBox "Move_basic=" (!FileExist(D "/m1.txt") && FileRead(D "/m2.txt") = "x")
FileAppend("y", D "/m3.txt")
try {
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileMove.htm](../../docs-v2/docs/lib/FileMove.htm)

````ahk
FileMove "C:\My Documents\List1.txt", "D:\Main Backup\"
````
