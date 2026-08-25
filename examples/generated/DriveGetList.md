# DriveGetList

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:183](../../tests/doccheck/assert_misc_cov.ahk#L183)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Uses lsblk; requires the external tool

Returns a string of letters, one character for each drive letter in the system.

## Syntax

````text
List := DriveGetList(DriveType)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("drivestatuscd", () => DriveGetStatusCD())
DriveListType() {
    l := DriveGetList()
    return Type(l)
}
Check("drivelist_type", DriveListType)
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveGetList.htm](../../docs-v2/docs/lib/DriveGetList.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
