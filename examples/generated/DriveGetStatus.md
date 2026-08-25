# DriveGetStatus

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:180](../../tests/doccheck/assert_misc_cov.ahk#L180)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Uses lsblk/udisksctl; requires the external tool

Returns the status of the drive which contains the specified path.

## Syntax

````text
Status := DriveGetStatus(Path)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("drivelabel", () => DriveGetLabel("/"))
Check("driveserial", () => DriveGetSerial("/"))
Check("drivestatus", () => DriveGetStatus("/"))
Check("drivestatuscd", () => DriveGetStatusCD())
DriveListType() {
    l := DriveGetList()
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveGetStatus.htm](../../docs-v2/docs/lib/DriveGetStatus.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
