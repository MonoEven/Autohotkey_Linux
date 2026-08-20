#Requires AutoHotkey v2.0
; minimal hotkey
Persistent(True)
F12:: FileAppend("f12`n", "/tmp/hk_f12.txt")