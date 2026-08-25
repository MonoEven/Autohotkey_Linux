# FileRecycle

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:268](../../tests/doccheck/assert_sys.ahk#L268)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Sends a file or directory to the recycle bin if possible, or permanently deletes it.

## Syntax

````text
FileRecycle FilePattern
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: FileRecycle moves the file to the recycle bin (XDG Trash on Linux).
FileRecycle(D "/src.txt")
MsgBox "Recycle_ok=" (FileExist(D "/src.txt") ? 0 : 1) (FileExist("/tmp/ahk_dc_data_r5/Trash/files/src.txt") ? 1 : 0)
MsgBox "Recycle_info=" (FileExist("/tmp/ahk_dc_data_r5/Trash/info/src.txt.trashinfo") ? 1 : 0)
; Docs: FileRecycleEmpty empties the recycle bin (OSError on failure).
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileRecycle.htm](../../docs-v2/docs/lib/FileRecycle.htm)

````ahk
FileRecycle "C:\temp files\*.tmp"
````
