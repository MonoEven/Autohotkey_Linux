# FileGetAttrib

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_file.ahk:62](../../tests/doccheck/assert_file.ahk#L62)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Reports whether a file or folder is read-only, hidden, etc.

## Syntax

````text
AttributeString := FileGetAttrib(Filename)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: FileGetAttrib returns attribute letters (A for archive on files).
MsgBox "Attrib_has_A=" (InStr(FileGetAttrib(fm), "A") != 0)
; Docs: FileSetAttrib "+R"/"-R" toggle read-only.
FileSetAttrib("+R", fm)
MsgBox "Attrib_after_R=" (InStr(FileGetAttrib(fm), "R") != 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileGetAttrib.htm](../../docs-v2/docs/lib/FileGetAttrib.htm)

````ahk
OutputVar := FileGetAttrib("C:\New Folder")
````
