# RegRead

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_registry.ahk:13](../../tests/doccheck/assert_registry.ahk#L13)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Registry is a single-user INI file (~/.config/autohotkey-registry.txt); all hives normalized into it

Reads a value from the registry.

## Syntax

````text
Value := RegRead(KeyName, ValueName, Default)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
n := RegWrite("hello", "REG_SZ", "HKCU\Software\DocCheck", "Str")
MsgBox "Write_sz=" (n > 0)
MsgBox "Read_sz=" (RegRead("HKCU\Software\DocCheck", "Str") = "hello")
; Docs: REG_DWORD is read as a positive decimal integer.
RegWrite(42, "REG_DWORD", "HKCU\Software\DocCheck", "Num")
MsgBox "Read_dword=" (RegRead("HKCU\Software\DocCheck", "Num") = 42)
````

## Upstream reference example

Source: [docs-v2/docs/lib/RegRead.htm](../../docs-v2/docs/lib/RegRead.htm)

````ahk
TestValue := RegRead("HKEY_LOCAL_MACHINE\Software\SomeApplication", "TestValue")
````
