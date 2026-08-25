# IniWrite

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:61](../../tests/doccheck/assert_general.ahk#L61)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Writes a value or section to a standard format .ini file.

## Syntax

````text
IniWrite Value, Filename, Section, Key IniWrite Pairs, Filename, Section
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; Ini
ini := "/tmp/ahk_dc_ini.ini"
IniWrite("v1", ini, "S", "K")
MsgBox "IniRead=" IniRead(ini, "S", "K")
MsgBox "IniRead_default=" IniRead(ini, "S", "NOPE", "dflt")
IniDelete(ini, "S", "K")
````

## Upstream reference example

Source: [docs-v2/docs/lib/IniWrite.htm](../../docs-v2/docs/lib/IniWrite.htm)

````ahk
IniWrite "this is a new value", "C:\Temp\myfile.ini", "section2", "key"
````
