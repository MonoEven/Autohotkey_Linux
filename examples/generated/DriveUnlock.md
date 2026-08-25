# DriveUnlock

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:336](../../tests/doccheck/assert_sys.ahk#L336)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses udisksctl; requires the external tool

Restores the eject feature of the specified drive.

## Syntax

````text
DriveUnlock Drive
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    MsgBox "DriveLock_err=OSError"
try
    DriveUnlock("/nonexistent")
catch OSError
    MsgBox "DriveUnlock_err=OSError"
try
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveUnlock.htm](../../docs-v2/docs/lib/DriveUnlock.htm)

````ahk
DriveUnlock "D:"
````
