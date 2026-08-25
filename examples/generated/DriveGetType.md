# DriveGetType

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:55](../../tests/doccheck/assert_general.ahk#L55)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses lsblk; requires the external tool

Returns the type of the drive which contains the specified path.

## Syntax

````text
DriveType := DriveGetType(Path)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Drive
MsgBox "DriveGetType=" (DriveGetType("/") != "")
MsgBox "DriveGetSpaceFree=" (DriveGetSpaceFree("/") > 0)
MsgBox "DriveGetCapacity=" (DriveGetCapacity("/") > 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveGetType.htm](../../docs-v2/docs/lib/DriveGetType.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
