# DriveGetStatusCD

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:181](../../tests/doccheck/assert_misc_cov.ahk#L181)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the media status of the specified CD/DVD drive.

## Syntax

````text
CDStatus := DriveGetStatusCD(Drive)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("driveserial", () => DriveGetSerial("/"))
Check("drivestatus", () => DriveGetStatus("/"))
Check("drivestatuscd", () => DriveGetStatusCD())
DriveListType() {
    l := DriveGetList()
    return Type(l)
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveGetStatusCD.htm](../../docs-v2/docs/lib/DriveGetStatusCD.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
