# SplitPath

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_string.ahk:48](../../tests/doccheck/assert_string.ahk#L48)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Separates a file name or URL into its name, directory, extension, and drive.

## Syntax

````text
SplitPath Path , &OutFileName, &OutDir, &OutExtension, &OutNameNoExt, &OutDrive
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Sort_comma=" Sort("c,b,a", "D,")
MsgBox "Sort_numeric=" Sort("3,1,2", "N D,")
MsgBox "SplitPath_name=" (SplitPath("/a/b/file.txt", &n, &d, &e, &ne, &dr), n)
MsgBox "SplitPath_dir=" (SplitPath("/a/b/file.txt", &n, &d, &e, &ne, &dr), d)
MsgBox "SplitPath_ext=" (SplitPath("/a/b/file.txt", &n, &d, &e, &ne, &dr), e)
MsgBox "SplitPath_noext=" (SplitPath("/a/b/file.txt", &n, &d, &e, &ne, &dr), ne)
````

## Upstream reference example

Source: [docs-v2/docs/lib/SplitPath.htm](../../docs-v2/docs/lib/SplitPath.htm)

````ahk
FullFileName := "C:\My Documents\Address List.txt"
; To fetch only the bare filename from the above:
SplitPath FullFileName, &name
; To fetch only its directory:
SplitPath FullFileName,, &dir
; To fetch all info:
SplitPath FullFileName, &name, &dir, &ext, &name_no_ext, &drive
; The abo
````
