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
