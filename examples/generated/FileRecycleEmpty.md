# FileRecycleEmpty

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:272](../../tests/doccheck/assert_sys.ahk#L272)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Empties the recycle bin.

## Syntax

````text
FileRecycleEmpty DriveLetter
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Recycle_info=" (FileExist("/tmp/ahk_dc_data_r5/Trash/info/src.txt.trashinfo") ? 1 : 0)
; Docs: FileRecycleEmpty empties the recycle bin (OSError on failure).
FileRecycleEmpty()
MsgBox "Recycle_empty=" (!FileExist("/tmp/ahk_dc_data_r5/Trash/files/src.txt"))
try
    FileRecycleEmpty()
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileRecycleEmpty.htm](../../docs-v2/docs/lib/FileRecycleEmpty.htm)

````ahk
FileRecycleEmpty "C:\"
````
