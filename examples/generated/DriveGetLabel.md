# DriveGetLabel

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:178](../../tests/doccheck/assert_misc_cov.ahk#L178)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the volume label of the specified drive.

## Syntax

````text
Label := DriveGetLabel(Drive)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("drivefs_root", () => DriveGetFileSystem("/") != "")
Check("drivefs2_root", () => DriveGetFilesystem("/") != "")
Check("drivelabel", () => DriveGetLabel("/"))
Check("driveserial", () => DriveGetSerial("/"))
Check("drivestatus", () => DriveGetStatus("/"))
Check("drivestatuscd", () => DriveGetStatusCD())
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveGetLabel.htm](../../docs-v2/docs/lib/DriveGetLabel.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
