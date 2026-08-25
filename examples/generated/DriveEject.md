# DriveEject

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:328](../../tests/doccheck/assert_sys.ahk#L328)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses eject/udisksctl; requires the external tool

Ejects or retracts the tray of the specified CD/DVD drive. DriveEject can also eject a removable drive.

## Syntax

````text
DriveEject Drive DriveRetract Drive
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    MsgBox "DriveSetLabel_err=OSError"
try
    DriveEject("/nonexistent")
catch OSError
    MsgBox "DriveEject_err=OSError"
try
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveEject.htm](../../docs-v2/docs/lib/DriveEject.htm)

````ahk
DriveEject()
````
