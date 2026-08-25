# RegDeleteKey

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_registry.ahk:59](../../tests/doccheck/assert_registry.ahk#L59)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Registry is a single-user INI file; no system-wide/permission/32-64 view semantics

Deletes a subkey from the registry.

## Syntax

````text
RegDeleteKey KeyName
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; RegDeleteKey removes the key including subkeys and values.
RegWrite("v", "REG_SZ", "HKCU\Software\DelKey\Sub", "K")
RegDeleteKey("HKCU\Software\DelKey")
try
    RegRead("HKCU\Software\DelKey\Sub", "K")
catch
````

## Upstream reference example

Source: [docs-v2/docs/lib/RegDeleteKey.htm](../../docs-v2/docs/lib/RegDeleteKey.htm)

````ahk
RegDeleteKey "HKEY_LOCAL_MACHINE\Software\SomeApplication"
````
