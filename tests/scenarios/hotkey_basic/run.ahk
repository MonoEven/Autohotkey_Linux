#Requires AutoHotkey v2.0
; Scenario hotkey_basic: a registered hotkey fires when the key is sent.
WINOUT := "/tmp/scn_hotkey_basic"
FileDelete(WINOUT)
cnt := 0
CB(ThisHotkey) {
    global cnt
    cnt++
}
Hotkey("F7", CB)
Sleep(200)
Send("{F7}")
Sleep(300)
if (cnt = 1)
    FileAppend("ok`n", WINOUT)
ExitApp
