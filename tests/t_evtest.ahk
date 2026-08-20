#Requires AutoHotkey v2.0
; evdev backend E2E: F12 + Ctrl+N fire callbacks; other keys pass through.
Persistent(True)
F12:: {
    FileAppend("f12`n", "/tmp/hk_f12.txt")
}
^N:: {
    FileAppend("cn`n", "/tmp/hk_cn.txt")
}