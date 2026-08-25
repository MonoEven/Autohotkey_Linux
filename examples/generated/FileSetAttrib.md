# FileSetAttrib

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_file.ahk:64](../../tests/doccheck/assert_file.ahk#L64)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Changes the attributes of one or more files or folders. Wildcards are supported.

## Syntax

````text
FileSetAttrib Attributes , FilePattern, Mode
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Attrib_has_A=" (InStr(FileGetAttrib(fm), "A") != 0)
; Docs: FileSetAttrib "+R"/"-R" toggle read-only.
FileSetAttrib("+R", fm)
MsgBox "Attrib_after_R=" (InStr(FileGetAttrib(fm), "R") != 0)
FileSetAttrib("-R", fm)
MsgBox "Attrib_after_unR=" (InStr(FileGetAttrib(fm), "R") = 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileSetAttrib.htm](../../docs-v2/docs/lib/FileSetAttrib.htm)

````ahk
FileSetAttrib "+RH", "C:\MyFiles\*.*", "DF"  ; +RH is identical to +R+H
````
