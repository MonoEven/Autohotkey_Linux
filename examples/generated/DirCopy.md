# DirCopy

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_file.ahk:35](../../tests/doccheck/assert_file.ahk#L35)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Copies a folder along with all its sub-folders and files (similar to xcopy) or the entire contents of an archive file such as ZIP.

## Syntax

````text
DirCopy Source, Dest , Overwrite
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
DirCreate("/tmp/ahk_dc_src")
FileAppend("x", "/tmp/ahk_dc_src/inside.txt")
DirCopy("/tmp/ahk_dc_src", "/tmp/ahk_dc_dst")
MsgBox "DirCopy=" (FileExist("/tmp/ahk_dc_dst/inside.txt") != "")
DirMove("/tmp/ahk_dc_dst", "/tmp/ahk_dc_moved")
MsgBox "DirMove=" (FileExist("/tmp/ahk_dc_moved/inside.txt") != "")
````

## Upstream reference example

Source: [docs-v2/docs/lib/DirCopy.htm](../../docs-v2/docs/lib/DirCopy.htm)

````ahk
DirCopy "C:\My Folder", "C:\Copy of My Folder"
````
