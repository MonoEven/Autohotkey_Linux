# DriveGetFilesystem

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:176](../../tests/doccheck/assert_misc_cov.ahk#L176)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Uses lsblk; requires the external tool

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- Drive (statvfs + /proc/mounts; "/" is always mounted) ---------------
Check("drivefs_root", () => DriveGetFileSystem("/") != "")
Check("drivefs2_root", () => DriveGetFilesystem("/") != "")
Check("drivelabel", () => DriveGetLabel("/"))
Check("driveserial", () => DriveGetSerial("/"))
````
