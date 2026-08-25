# DriveGetCapacity

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:57](../../tests/doccheck/assert_general.ahk#L57)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses lsblk/df; requires the external tool

Returns the total capacity of the drive which contains the specified path, in megabytes.

## Syntax

````text
Capacity := DriveGetCapacity(Path)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "DriveGetType=" (DriveGetType("/") != "")
MsgBox "DriveGetSpaceFree=" (DriveGetSpaceFree("/") > 0)
MsgBox "DriveGetCapacity=" (DriveGetCapacity("/") > 0)

; Ini
ini := "/tmp/ahk_dc_ini.ini"
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveGetCapacity.htm](../../docs-v2/docs/lib/DriveGetCapacity.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
