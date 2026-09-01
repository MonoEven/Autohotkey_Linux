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
; Windows-golden self-trigger path (official HotInputLevelAllowsFiring): a
; synthetic event fires a hotkey only when send_level > input_level, so a
; level-0 Send can never fire a level-0 hotkey.  SendEvent at SendLevel 1 is
; the documented way for a script to trigger its own hotkeys.
SendMode("Event")
SendLevel(1)
Sleep(200)
Send("{F7}")
Sleep(300)
if (cnt = 1)
    FileAppend("ok`n", WINOUT)
ExitApp
