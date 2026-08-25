# FileEncoding

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:126](../../tests/doccheck/assert_sys.ahk#L126)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Sets the default encoding for FileRead, Loop Read, FileAppend, and FileOpen.

## Syntax

````text
PrevEncoding := FileEncoding(Encoding)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: FileEncoding returns the previous setting ("CP0"/"UTF-8"/...).
MsgBox "FileEncoding_prev=" (FileEncoding("UTF-8") = "CP0")
MsgBox "FileEncoding_set=" (A_FileEncoding = "UTF-8")
MsgBox "FileEncoding_return=" (FileEncoding("UTF-8-RAW") = "UTF-8")
MsgBox "FileEncoding_restore=" (FileEncoding("CP0") = "UTF-8-RAW")
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileEncoding.htm](../../docs-v2/docs/lib/FileEncoding.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
