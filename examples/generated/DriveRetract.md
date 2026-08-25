# DriveRetract

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:340](../../tests/doccheck/assert_sys.ahk#L340)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses eject/udisksctl; requires the external tool

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    MsgBox "DriveUnlock_err=OSError"
try
    DriveRetract("/nonexistent")
catch OSError
    MsgBox "DriveRetract_err=OSError"
try
````
