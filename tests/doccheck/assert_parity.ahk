; Parity classification doc-check (check_detail0821 §13): the four-level
; parity model (P1 compatible / P2 adapted / P3 simulated / P4 unavailable)
; must be queryable at runtime -- `ahk_core --parity FuncName` prints the
; level + note, and A_ParityLevel(FuncName) returns the level (1..4, P1
; default for anything not in the classification table).
#Requires AutoHotkey v2.0

; --- `ahk_core --parity FuncName` CLI ---
parity_out := "/tmp/ahk_dc_parity_out.txt"
FileDelete(parity_out)
RunWait('sh -c "' A_AhkPath ' --parity ComObjArray > ' parity_out ' 2>&1"', , "Hide")
t_p4 := FileRead(parity_out)
MsgBox "parity_cli_p4=" (InStr(t_p4, "P4 unavailable") && InStr(t_p4, "ComObjArray") ? 1 : 0)
FileDelete(parity_out)
RunWait('sh -c "' A_AhkPath ' --parity SendInput > ' parity_out ' 2>&1"', , "Hide")
t_p2 := FileRead(parity_out)
MsgBox "parity_cli_p2=" (InStr(t_p2, "P2 adapted") ? 1 : 0)
FileDelete(parity_out)
RunWait('sh -c "' A_AhkPath ' --parity RegRead > ' parity_out ' 2>&1"', , "Hide")
t_p3 := FileRead(parity_out)
MsgBox "parity_cli_p3=" (InStr(t_p3, "P3 simulated") ? 1 : 0)
FileDelete(parity_out)
RunWait('sh -c "' A_AhkPath ' --parity MsgBox > ' parity_out ' 2>&1"', , "Hide")
t_p1 := FileRead(parity_out)
MsgBox "parity_cli_p1=" (InStr(t_p1, "P1 compatible") ? 1 : 0)

; --- A_ParityLevel(FuncName) in-script ---
MsgBox "parity_level_p4=" (A_ParityLevel("ComObjArray") = 4 ? 1 : 0)
MsgBox "parity_level_p2=" (A_ParityLevel("SendPlay") = 2 ? 1 : 0)
MsgBox "parity_level_p3=" (A_ParityLevel("RegRead") = 3 ? 1 : 0)
MsgBox "parity_level_p1=" (A_ParityLevel("MsgBox") = 1 ? 1 : 0)
MsgBox "parity_level_default=" (A_ParityLevel("NoSuchFunction") = 1 ? 1 : 0)
