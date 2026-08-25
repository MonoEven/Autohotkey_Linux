# DirMove

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_file.ahk:37](../../tests/doccheck/assert_file.ahk#L37)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Moves a folder along with all its sub-folders and files. It can also rename a folder.

## Syntax

````text
DirMove Source, Dest , OverwriteOrRename
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
DirCopy("/tmp/ahk_dc_src", "/tmp/ahk_dc_dst")
MsgBox "DirCopy=" (FileExist("/tmp/ahk_dc_dst/inside.txt") != "")
DirMove("/tmp/ahk_dc_dst", "/tmp/ahk_dc_moved")
MsgBox "DirMove=" (FileExist("/tmp/ahk_dc_moved/inside.txt") != "")
MsgBox "DirMove_srcgone=" (DirExist("/tmp/ahk_dc_dst") = "")
DirDelete("/tmp/ahk_dc_moved", true)
````

## Upstream reference example

Source: [docs-v2/docs/lib/DirMove.htm](../../docs-v2/docs/lib/DirMove.htm)

````ahk
DirMove "C:\My Folder", "D:\My Folder"
````
