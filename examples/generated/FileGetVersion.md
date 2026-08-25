# FileGetVersion

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:282](../../tests/doccheck/assert_sys.ahk#L282)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Retrieves the version of a file.

## Syntax

````text
Version := FileGetVersion(Filename)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; (all Linux files lack a Windows version resource).
try
    FileGetVersion(D "/dst.txt")
catch OSError
    MsgBox "FileGetVersion_noversion=OSError"
try
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileGetVersion.htm](../../docs-v2/docs/lib/FileGetVersion.htm)

````ahk
Version := FileGetVersion("C:\My Application.exe")
````
