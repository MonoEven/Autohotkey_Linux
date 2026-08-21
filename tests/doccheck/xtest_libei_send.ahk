#Requires AutoHotkey v2.0
; xtest_libei_send.ahk -- R1-7: send A_Args[1] through the XTEST path.
; GUI-less: connects to DISPLAY (Xwayland) purely for XTestFakeKeyEvent.
; Writes SEND_OK=<text> or SEND_ERR=<msg> to /tmp/xtest_libei_send_done.txt.
Persistent
txt := A_Args.Length ? A_Args[1] : "SENDDEFAULT"
result := "SEND_ERR=unknown"
try {
    SendText(txt)
    result := "SEND_OK=" txt
} catch as e {
    result := "SEND_ERR=" (e ? e.Message : "unknown")
}
FileAppend(result "`n", "/tmp/xtest_libei_send_done.txt")
Sleep 300
ExitApp
