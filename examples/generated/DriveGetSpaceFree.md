# DriveGetSpaceFree

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:56](../../tests/doccheck/assert_general.ahk#L56)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses lsblk/df; requires the external tool

Returns the free disk space of the drive which contains the specified path, in megabytes.

## Syntax

````text
FreeSpace := DriveGetSpaceFree(Path)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; Drive
MsgBox "DriveGetType=" (DriveGetType("/") != "")
MsgBox "DriveGetSpaceFree=" (DriveGetSpaceFree("/") > 0)
MsgBox "DriveGetCapacity=" (DriveGetCapacity("/") > 0)

; Ini
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveGetSpaceFree.htm](../../docs-v2/docs/lib/DriveGetSpaceFree.htm)

````ahk
FreeSpace := DriveGetSpaceFree(A_MyDocuments)
MsgBox FreeSpace " MB"
````
