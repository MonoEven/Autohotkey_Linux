; File module doc-check (v2 docs: FileExist/DirExist/DirCreate/DirDelete/
; FileRead/FileAppend/FileOpen/FileDelete/DirCopy/DirMove).
#Requires AutoHotkey v2.0

MsgBox "FileExist_yes=" (FileExist("/tmp") != "")
MsgBox "FileExist_no=" FileExist("/nonexistent-xyz-123")
MsgBox "DirExist_yes=" (DirExist("/tmp") != "")
MsgBox "DirExist_no=" DirExist("/nonexistent-xyz-123")

DirCreate("/tmp/ahk_dc")
MsgBox "DirCreate=" (DirExist("/tmp/ahk_dc") != "")
DirDelete("/tmp/ahk_dc")
MsgBox "DirDelete=" (DirExist("/tmp/ahk_dc") = "")

f := "/tmp/ahk_dc_file.txt"
FileAppend("line1`n", f)
FileAppend("line2", f)
MsgBox "FileRead_ok=" (FileRead(f) = "line1`nline2")
MsgBox "FileExist_file=" (FileExist(f) != "")
FileDelete(f)
MsgBox "FileDelete=" (FileExist(f) = "")

; FileOpen: write then read back through a File object.
fo := FileOpen("/tmp/ahk_dc_fo.txt", "w")
fo.Write("hello-file")
fo.Close()
fr := FileOpen("/tmp/ahk_dc_fo.txt", "r")
MsgBox "FileOpen_read=" fr.Read()
fr.Close()
FileDelete("/tmp/ahk_dc_fo.txt")

; DirCopy / DirMove round-trip
DirCreate("/tmp/ahk_dc_src")
FileAppend("x", "/tmp/ahk_dc_src/inside.txt")
DirCopy("/tmp/ahk_dc_src", "/tmp/ahk_dc_dst")
MsgBox "DirCopy=" (FileExist("/tmp/ahk_dc_dst/inside.txt") != "")
DirMove("/tmp/ahk_dc_dst", "/tmp/ahk_dc_moved")
MsgBox "DirMove=" (FileExist("/tmp/ahk_dc_moved/inside.txt") != "")
MsgBox "DirMove_srcgone=" (DirExist("/tmp/ahk_dc_dst") = "")
DirDelete("/tmp/ahk_dc_moved", true)
DirDelete("/tmp/ahk_dc_src", true)

; --- FileGetTime/FileSetTime/FileGetSize/FileGetAttrib/FileSetAttrib + Loop Files ---
DirCreate("/tmp/ahk_dc_meta")
fm := "/tmp/ahk_dc_meta/data.txt"
FileAppend("0123456789", fm)  ; 10 bytes

; Docs: FileGetTime returns YYYYMMDDHH24MISS (14 digits), local time.
MsgBox "GetTime_len=" (StrLen(FileGetTime(fm)) = 14)
MsgBox "GetTime_A_len=" (StrLen(FileGetTime(fm, "A")) = 14)
MsgBox "GetTime_C_len=" (StrLen(FileGetTime(fm, "C")) = 14)

; Docs: FileSetTime sets the timestamp given as YYYYMMDDHH24MISS (local).
FileSetTime("20200102030405", fm)
MsgBox "SetTime_M=" (FileGetTime(fm) = "20200102030405")

; Docs: FileGetSize units B (default) / K / M, rounded down.
MsgBox "GetSize_B=" (FileGetSize(fm) = 10)
MsgBox "GetSize_K=" (FileGetSize(fm, "K") = 0)

; Docs: FileGetAttrib returns attribute letters (A for archive on files).
MsgBox "Attrib_has_A=" (InStr(FileGetAttrib(fm), "A") != 0)
; Docs: FileSetAttrib "+R"/"-R" toggle read-only.
FileSetAttrib("+R", fm)
MsgBox "Attrib_after_R=" (InStr(FileGetAttrib(fm), "R") != 0)
FileSetAttrib("-R", fm)
MsgBox "Attrib_after_unR=" (InStr(FileGetAttrib(fm), "R") = 0)

; Docs: FileSetTime supports wildcard patterns.
FileAppend("x", "/tmp/ahk_dc_meta/b.txt")
FileSetTime("20210101000000", "/tmp/ahk_dc_meta/*.txt")
MsgBox "WC_time=" (FileGetTime("/tmp/ahk_dc_meta/b.txt") = "20210101000000")

; Docs: Loop Files iterates matching files; A_LoopFileDir/Ext/Name reflect
; the current file.
loop_count := 0
loop_names := ""
loop_dir_ok := 0
loop_ext_ok := 0
Loop Files "/tmp/ahk_dc_meta/*.txt"
{
    loop_count += 1
    loop_names .= A_LoopFileName ","
    if (A_LoopFileDir = "/tmp/ahk_dc_meta")
        loop_dir_ok := 1
    if (A_LoopFileExt = "txt")
        loop_ext_ok := 1
}
MsgBox "Loop_count=" loop_count
MsgBox "Loop_has_b=" (InStr(loop_names, "b.txt") != 0)
MsgBox "Loop_has_data=" (InStr(loop_names, "data.txt") != 0)
MsgBox "Loop_dir_ok=" loop_dir_ok
MsgBox "Loop_ext_ok=" loop_ext_ok

; Docs: FileGetSize/FileGetAttrib/FileGetTime throw OSError on failure.
try
    FileGetSize("/nonexistent-xyz-123")
catch
    MsgBox "GetSize_missing_err=1"
try
    FileGetAttrib("/nonexistent-xyz-123")
catch
    MsgBox "GetAttrib_missing_err=1"
try
    FileGetTime("/nonexistent-xyz-123")
catch
    MsgBox "GetTime_missing_err=1"

DirDelete("/tmp/ahk_dc_meta", true)
