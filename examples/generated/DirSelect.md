# DirSelect

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_dialog.ahk:54](../../tests/doccheck/assert_dialog.ahk#L54)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Displays a standard dialog  that allows the user to select a folder.

## Syntax

````text
SelectedFolder := DirSelect(StartingFolder, Options, Prompt)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- DirSelect: returns the chosen folder (pre-filled from StartingFolder) ---
Log("ds_path=" (DirSelect("/tmp/ahk_dc_dir") = "/tmp/ahk_dc_dir" ? 1 : 0))
; "root*initial": the part after '*' is the initial folder (docs).
Log("ds_initial=" (DirSelect("/rootpart*/tmp/ahk_dc_initial") = "/tmp/ahk_dc_initial" ? 1 : 0))
; Options: 0 disables the create-new-folder button (accepted, no effect on
````

## Upstream reference example

Source: [docs-v2/docs/lib/DirSelect.htm](../../docs-v2/docs/lib/DirSelect.htm)

````ahk
SelectedFolder := DirSelect(, 3)
if SelectedFolder = ""
    MsgBox "You didn't select a folder."
else
    MsgBox "You selected folder '" SelectedFolder "'."
````
