# DriveSetLabel

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:324](../../tests/doccheck/assert_sys.ahk#L324)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses lsblk/e2label; requires the external tool

Changes the volume label of the specified drive.

## Syntax

````text
DriveSetLabel Drive , NewLabel
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

try
    DriveSetLabel("/nonexistent", "X")
catch OSError
    MsgBox "DriveSetLabel_err=OSError"
try
````

## Upstream reference example

Source: [docs-v2/docs/lib/DriveSetLabel.htm](../../docs-v2/docs/lib/DriveSetLabel.htm)

````ahk
DriveSetLabel "C:", "Seagate200"
````
