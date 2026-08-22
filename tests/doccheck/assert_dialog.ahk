; FileSelect/DirSelect doc-check (v2 docs).  Runs under Xvfb (run_check.sh
; --xvfb).  The Linux port implements the standard dialogs as a built-in
; X11 path-entry dialog (headless: stdin fallback); the AHK_FILESELECT_
; AUTOCLOSE_MS test hook auto-confirms the dialog with the pre-filled entry
; (initial dir + default name), so each call below verifies the documented
; parameter semantics end-to-end.  Output goes to a file.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_dialog_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

EnvSet("AHK_FILESELECT_AUTOCLOSE_MS", "800")

; --- FileSelect: returns the chosen path (pre-filled from RootDir\Filename) ---
Log("fs_path=" (FileSelect("", "/tmp/ahk_dc_pick.txt") = "/tmp/ahk_dc_pick.txt" ? 1 : 0))
; An existing directory as RootDir is the initial directory (docs).
Log("fs_dir=" (FileSelect(0, "/tmp") = "/tmp" ? 1 : 0))
; Numeric option bits (1 = FileMustExist, docs) are accepted; the flags have
; no effect on the Linux path-entry dialog (documented).
Log("fs_flags=" (FileSelect("1", "/tmp/ahk_dc_pick.txt") = "/tmp/ahk_dc_pick.txt" ? 1 : 0))
; M: multi-select returns an Array of paths (docs).
r := FileSelect("M", "/tmp/ahk_dc_multi.txt")
Log("fs_multi_type=" (Type(r) = "Array" ? 1 : 0))
Log("fs_multi_len=" (r.Length = 1 ? 1 : 0))
Log("fs_multi_0=" (r[1] = "/tmp/ahk_dc_multi.txt" ? 1 : 0))
; S: save-dialog letter accepted.
Log("fs_save=" (FileSelect("S", "/tmp/ahk_dc_save.txt") = "/tmp/ahk_dc_save.txt" ? 1 : 0))
; With no RootDir the entry is empty: confirming yields an empty string.
Log("fs_empty=" (FileSelect() = "" ? 1 : 0))

; --- ValueError: D cannot be combined with a Filter (upstream FR_E_ARG(3)) ---
try {
    FileSelect("D",, "t", "Text (*.txt)")
    Log("fs_d_filter=0")
} catch ValueError {
    Log("fs_d_filter=1")
}
; --- ValueError: non-numeric options (upstream FR_E_ARG(0)) ---
try {
    FileSelect("Z")
    Log("fs_badopt=0")
} catch ValueError {
    Log("fs_badopt=1")
}
try {
    FileSelect("DM")
    Log("fs_dm=0")
} catch ValueError {
    Log("fs_dm=1")
}

; --- DirSelect: returns the chosen folder (pre-filled from StartingFolder) ---
Log("ds_path=" (DirSelect("/tmp/ahk_dc_dir") = "/tmp/ahk_dc_dir" ? 1 : 0))
; "root*initial": the part after '*' is the initial folder (docs).
Log("ds_initial=" (DirSelect("/rootpart*/tmp/ahk_dc_initial") = "/tmp/ahk_dc_initial" ? 1 : 0))
; Options: 0 disables the create-new-folder button (accepted, no effect on
; the Linux dialog; documented).
Log("ds_opt0=" (DirSelect("/tmp/ahk_dc_dir2", 0) = "/tmp/ahk_dc_dir2" ? 1 : 0))
; Custom prompt.
Log("ds_prompt=" (DirSelect("/tmp/ahk_dc_dir3", 1, "Choose a folder") = "/tmp/ahk_dc_dir3" ? 1 : 0))
; No starting folder: initial selection is the user's home directory (the
; Linux counterpart of "My Documents" in the docs).
Log("ds_home=" (DirSelect() = EnvGet("HOME") ? 1 : 0))

; --- Cleanup. ---
ExitApp(0)
