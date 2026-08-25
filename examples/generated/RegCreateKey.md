# RegCreateKey

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_registry.ahk:44](../../tests/doccheck/assert_registry.ahk#L44)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Registry is a single-user INI file; no system-wide/permission/32-64 view semantics

Creates a registry key without writing a value.

## Syntax

````text
RegCreateKey KeyName
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    MsgBox "Read_missing_key_err=1"
; RegCreateKey
RegCreateKey("HKCU\Software\Created")
MsgBox "Create=" (RegRead("HKCU\Software\Created", "X", "created-ok") = "created-ok")
; RegDelete deletes the named value; deleting a missing value throws.
RegWrite("bye", "REG_SZ", "HKCU\Software\DocCheck", "Temp")
````

## Upstream reference example

Source: [docs-v2/docs/lib/RegCreateKey.htm](../../docs-v2/docs/lib/RegCreateKey.htm)

````ahk
RegCreateKey "HKCU\Software\Classes\.ahk\OpenWithList\notepad++.exe"
````
