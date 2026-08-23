#Requires AutoHotkey v2.0
; Scenario a_and_b: the "a & b" custom combo is unsupported on X11 -- the
; registration must raise a clear error naming the combo (documented), not a
; silent no-op.
WINOUT := "/tmp/scn_a_and_b"
FileDelete(WINOUT)
try {
    Hotkey("a & b", (*) => 0)
    FileAppend("noconflict`n", WINOUT)
} catch OSError as e {
    FileAppend("err:`n", WINOUT)
}
ExitApp
