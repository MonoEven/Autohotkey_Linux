# RegWrite

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_registry.ahk:11](../../tests/doccheck/assert_registry.ahk#L11)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Registry is a single-user INI file; no system-wide/permission/32-64 view semantics

Writes a value to the registry.

## Syntax

````text
RegWrite Value, ValueType, KeyName , ValueName RegWrite Value , ValueType, , ValueName
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; RegWrite(Value, ValueType, KeyName, ValueName) returns bytes written.
n := RegWrite("hello", "REG_SZ", "HKCU\Software\DocCheck", "Str")
MsgBox "Write_sz=" (n > 0)
MsgBox "Read_sz=" (RegRead("HKCU\Software\DocCheck", "Str") = "hello")
; Docs: REG_DWORD is read as a positive decimal integer.
````

## Upstream reference example

Source: [docs-v2/docs/lib/RegWrite.htm](../../docs-v2/docs/lib/RegWrite.htm)

````ahk
RegWrite "Test Value", "REG_SZ", "HKEY_LOCAL_MACHINE\SOFTWARE\TestKey", "MyValueName"
````
