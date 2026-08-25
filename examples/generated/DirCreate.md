# DirCreate

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_file.ahk:10](../../tests/doccheck/assert_file.ahk#L10)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_image.ahk:26](../../tests/doccheck/assert_image.ahk#L26)
- `headless`: [tests/doccheck/assert_sys.ahk:138](../../tests/doccheck/assert_sys.ahk#L138)

Creates a folder.

## Syntax

````text
DirCreate DirName
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "DirExist_no=" DirExist("/nonexistent-xyz-123")

DirCreate("/tmp/ahk_dc")
MsgBox "DirCreate=" (DirExist("/tmp/ahk_dc") != "")
DirDelete("/tmp/ahk_dc")
MsgBox "DirDelete=" (DirExist("/tmp/ahk_dc") = "")
````

## Upstream reference example

Source: [docs-v2/docs/lib/DirCreate.htm](../../docs-v2/docs/lib/DirCreate.htm)

````ahk
DirCreate "C:\Test1\My Images\Folder2"
````
