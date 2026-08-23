#Requires AutoHotkey v2.0
; Scenario clipboard_roundtrip: A_Clipboard round-trips a large UTF-8 string.
WINOUT := "/tmp/scn_clipboard_roundtrip"
FileDelete(WINOUT)
big := ""
Loop 5000
    big .= "中文测试abc123"
A_Clipboard := big
Sleep(400)
ok := (A_Clipboard = big)
if (ok)
    FileAppend("ok`n", WINOUT)
ExitApp
