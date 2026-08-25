# DirDelete

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_file.ahk:12](../../tests/doccheck/assert_file.ahk#L12)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_sys.ahk:137](../../tests/doccheck/assert_sys.ahk#L137)

Deletes a folder.

## Syntax

````text
DirDelete DirName , Recurse
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
DirCreate("/tmp/ahk_dc")
MsgBox "DirCreate=" (DirExist("/tmp/ahk_dc") != "")
DirDelete("/tmp/ahk_dc")
MsgBox "DirDelete=" (DirExist("/tmp/ahk_dc") = "")

f := "/tmp/ahk_dc_file.txt"
````

## Upstream reference example

Source: [docs-v2/docs/lib/DirDelete.htm](../../docs-v2/docs/lib/DirDelete.htm)

````ahk
DirDelete "C:\Download Temp"
````
