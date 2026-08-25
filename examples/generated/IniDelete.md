# IniDelete

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:64](../../tests/doccheck/assert_general.ahk#L64)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Deletes a value from a standard format .ini file.

## Syntax

````text
IniDelete Filename, Section , Key
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "IniRead=" IniRead(ini, "S", "K")
MsgBox "IniRead_default=" IniRead(ini, "S", "NOPE", "dflt")
IniDelete(ini, "S", "K")
MsgBox "IniDelete=" (IniRead(ini, "S", "K") = "")
FileDelete(ini)
````

## Upstream reference example

Source: [docs-v2/docs/lib/IniDelete.htm](../../docs-v2/docs/lib/IniDelete.htm)

````ahk
IniDelete "C:\Temp\myfile.ini", "section2", "key"
````
