#Requires AutoHotkey v2.0
; Scenario keydown_longpress: holding a key fires it repeatedly (detectable
; auto-repeat); the count must be in a sane window.  Uses a plain hotkey (in
; v2 "X down" is an invalid key name -- it raises an error dialog, not a
; long-press).  Also verifies the invalid "down" suffix raises.
WINOUT := "/tmp/scn_keydown_longpress"
FileDelete(WINOUT)
cnt := 0
CB(ThisHotkey) {
    global cnt
    cnt++
}
Hotkey("F7", CB)
Sleep(200)
Send("{F7 down}")
Sleep(1200)
Send("{F7 up}")
Sleep(300)
; Detectable auto-repeat yields repeated fires while held (typically 5-40/s).
if (cnt >= 3 && cnt <= 150)
    FileAppend("ok:" cnt "`n", WINOUT)
ExitApp