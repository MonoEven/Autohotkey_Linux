#Requires AutoHotkey v2.0
; Feature smoke test for the Linux port.
out := "/tmp/ahk_linux_test.txt"

; --- File built-ins (WIP) ---
DirCreate("/tmp/ahk_test_dir")
FileAppend("line1`n", out)
FileAppend("line2`n", out)
content := FileRead(out)
MsgBox "FileRead = " content

exists := FileExist(out)
MsgBox "FileExist = " exists
MsgBox "DirExist = " DirExist("/tmp/ahk_test_dir")

; --- String built-ins ---
s := "  Hello, AutoHotkey on Linux!  "
MsgBox StrLen(s) " | " SubStr(s, 1, 5) " | " InStr(s, "Auto") " | " Trim(s)
MsgBox StrReplace(s, "Linux", "GNU/Linux")

; --- Math built-ins ---
MsgBox Abs(-42) " " Sqrt(16) " " Round(3.14159, 2) " " Mod(17, 5) " " Min(3, 9) " " Max(3, 9)

; --- Loops / control flow ---
sum := 0
loop 10
    sum += A_Index
MsgBox "sum 1..10 = " sum

; --- Objects ---
o := {name: "ahk", ver: 2}
o.extra := "linux"
MsgBox o.name " v" o.ver " " o.extra

; --- Arrays ---
arr := [10, 20, 30]
total := 0
for v in arr
    total += v
MsgBox "array sum = " total

; --- Format ---
MsgBox Format("{1} + {2} = {3}", 2, 3, 2 + 3)

MsgBox "ALL FEATURES OK"
ExitApp 0
