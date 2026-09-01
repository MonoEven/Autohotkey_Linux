#Requires AutoHotkey v2.0
; Scenario hotkey_thisprior: A_ThisHotkey / A_PriorHotkey reflect the fired pair.
WINOUT := "/tmp/scn_hotkey_thisprior"
FileDelete(WINOUT)
ok := 0
CB8(ThisHotkey) {
    global ok
}
Hotkey("F8", CB8)
Sleep(200)
CB9(ThisHotkey) {
    global ok
    if (A_ThisHotkey = "F9" && A_PriorHotkey = "F8")
        ok := 1
}
Hotkey("F9", CB9)
; Windows-golden self-trigger path: SendEvent at SendLevel 1 fires level-0
; hotkeys (a level-0 Send may not -- send_level must exceed input_level).
SendMode("Event")
SendLevel(1)
Sleep(200)
Send("{F8}")
Sleep(300)
Send("{F9}")
Sleep(300)
if (ok = 1)
    FileAppend("ok`n", WINOUT)
ExitApp
