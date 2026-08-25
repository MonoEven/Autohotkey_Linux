# DirExist

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_file.ahk:7](../../tests/doccheck/assert_file.ahk#L7)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Checks for the existence of a folder and returns its attributes.

## Syntax

````text
AttributeString := DirExist(FilePattern)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "FileExist_yes=" (FileExist("/tmp") != "")
MsgBox "FileExist_no=" FileExist("/nonexistent-xyz-123")
MsgBox "DirExist_yes=" (DirExist("/tmp") != "")
MsgBox "DirExist_no=" DirExist("/nonexistent-xyz-123")

DirCreate("/tmp/ahk_dc")
````

## Upstream reference example

Source: [docs-v2/docs/lib/DirExist.htm](../../docs-v2/docs/lib/DirExist.htm)

````ahk
if DirExist("C:\Windows")
    MsgBox "The target folder does exist."
````
