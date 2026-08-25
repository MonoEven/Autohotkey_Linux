# FileInstall

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:260](../../tests/doccheck/assert_sys.ahk#L260)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Includes the specified file inside the compiled version of the script.

## Syntax

````text
FileInstall Source, Dest , Overwrite
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: FileInstall in an uncompiled script copies the file to the target.
FileInstall(D "/src.txt", D "/inst.txt")
MsgBox "Install_copy=" (FileRead(D "/inst.txt") = "hello")
try
    FileInstall(D "/src.txt", D "/inst.txt")
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileInstall.htm](../../docs-v2/docs/lib/FileInstall.htm)

````ahk
FileInstall "My File.txt", A_Desktop "\Example File.txt", 1
````
