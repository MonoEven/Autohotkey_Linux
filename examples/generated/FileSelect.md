# FileSelect

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_dialog.ahk:16](../../tests/doccheck/assert_dialog.ahk#L16)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Displays a standard dialog  that allows the user to open or save file(s).

## Syntax

````text
SelectedFile := FileSelect(Options, RootDir\Filename, Title, Filter)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- FileSelect: returns the chosen path (pre-filled from RootDir\Filename) ---
Log("fs_path=" (FileSelect("", "/tmp/ahk_dc_pick.txt") = "/tmp/ahk_dc_pick.txt" ? 1 : 0))
; An existing directory as RootDir is the initial directory (docs).
Log("fs_dir=" (FileSelect(0, "/tmp") = "/tmp" ? 1 : 0))
; Numeric option bits (1 = FileMustExist, docs) are accepted; the flags have
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileSelect.htm](../../docs-v2/docs/lib/FileSelect.htm)

````ahk
SelectedFile := FileSelect(3, , "Open a file", "Text Documents (*.txt; *.doc)")
if SelectedFile = ""
    MsgBox "The dialog was canceled."
else
    MsgBox "The following file was selected:`n" SelectedFile
````
