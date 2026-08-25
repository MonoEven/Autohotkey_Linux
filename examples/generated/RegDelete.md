# RegDelete

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_registry.ahk:48](../../tests/doccheck/assert_registry.ahk#L48)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Registry is a single-user INI file; no system-wide/permission/32-64 view semantics

Deletes a value from the registry.

## Syntax

````text
RegDelete KeyName, ValueName
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; RegDelete deletes the named value; deleting a missing value throws.
RegWrite("bye", "REG_SZ", "HKCU\Software\DocCheck", "Temp")
RegDelete("HKCU\Software\DocCheck", "Temp")
try
    RegRead("HKCU\Software\DocCheck", "Temp")
catch
````

## Upstream reference example

Source: [docs-v2/docs/lib/RegDelete.htm](../../docs-v2/docs/lib/RegDelete.htm)

````ahk
RegDelete "HKEY_LOCAL_MACHINE\Software\SomeApplication", "TestValue"
````
