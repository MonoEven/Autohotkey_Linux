; Child used by assert_hotkey_pt (check0820 kill-9 release test): grabs F12
; exclusively and stays alive until killed.
#Requires AutoHotkey v2.0
Persistent(True)
F12:: {
    FileAppend("child-fired`n", "/tmp/ahk_kill9_child_fired.txt")
}