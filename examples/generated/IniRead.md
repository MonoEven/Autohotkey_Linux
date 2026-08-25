# IniRead

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:62](../../tests/doccheck/assert_general.ahk#L62)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Reads a value, section or list of section names from a standard format .ini file.

## Syntax

````text
Value := IniRead(Filename, Section, Key , Default) Section := IniRead(Filename, Section ,, Default) SectionNames := IniRead(Filename ,,, Default)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ini := "/tmp/ahk_dc_ini.ini"
IniWrite("v1", ini, "S", "K")
MsgBox "IniRead=" IniRead(ini, "S", "K")
MsgBox "IniRead_default=" IniRead(ini, "S", "NOPE", "dflt")
IniDelete(ini, "S", "K")
MsgBox "IniDelete=" (IniRead(ini, "S", "K") = "")
````

## Upstream reference example

Source: [docs-v2/docs/lib/IniRead.htm](../../docs-v2/docs/lib/IniRead.htm)

````ahk
Value := IniRead("C:\Temp\myfile.ini", "section2", "key")
MsgBox "The value is " Value
````
