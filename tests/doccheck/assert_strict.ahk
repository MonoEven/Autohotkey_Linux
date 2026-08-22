; AHK_STRICT_PARITY regression (check_detail0821 §13 / R2): the migration aid
; must turn "fake compatibility" into an explicit list -- with =error, the
; first call of a P3/P4 function raises; P1/P2 functions are unaffected.
#Requires AutoHotkey v2.0

; The strict mode is read once (cached), so test a single mode per process.
EnvSet("AHK_STRICT_PARITY", "error")

; InstallKeybdHook is P3 simulated: the first call must raise.
try {
    InstallKeybdHook()
    MsgBox "strict_err_ok=0"
} catch as e {
    MsgBox "strict_err_ok=" (InStr(e.Message, "AHK_STRICT_PARITY") ? 1 : 0)
}
; A P1 function is unaffected by strict parity.
try {
    n := A_ParityLevel("MsgBox")
    MsgBox "strict_p1_ok=" (n = 1 ? 1 : 0)
} catch {
    MsgBox "strict_p1_ok=0"
}
; A P2 function (adapted, not simulated) is also unaffected.
try {
    n2 := A_ParityLevel("SendInput")
    MsgBox "strict_p2_ok=" (n2 = 2 ? 1 : 0)
} catch {
    MsgBox "strict_p2_ok=0"
}
