# DriveLock

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:332](../../tests/doccheck/assert_sys.ahk#L332)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses udisksctl; requires the external tool

Prevents the eject feature of the specified drive from working.

## Syntax

````text
DriveLock Drive
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    MsgBox "DriveEject_err=OSError"
try
    DriveLock("/nonexistent")
catch OSError
    MsgBox "DriveLock_err=OSError"
try
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveLock.htm](../../docs-v2/docs/lib/DriveLock.htm)

````ahk
DriveLock "D:"
````
