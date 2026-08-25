# DriveGetFileSystem

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:176](../../tests/doccheck/assert_misc_cov.ahk#L176)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the type of the specified drive's file system.

## Syntax

````text
FileSystem := DriveGetFileSystem(Drive)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- Drive (statvfs + /proc/mounts; "/" is always mounted) ---------------
Check("drivefs_root", () => DriveGetFileSystem("/") != "")
Check("drivefs2_root", () => DriveGetFilesystem("/") != "")
Check("drivelabel", () => DriveGetLabel("/"))
Check("driveserial", () => DriveGetSerial("/"))
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveGetFileSystem.htm](../../docs-v2/docs/lib/DriveGetFileSystem.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
