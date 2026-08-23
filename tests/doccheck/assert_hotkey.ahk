; Hotkey module doc-check (v2 docs: Hotkey function + XGrabKey activation).
; Runs under Xvfb (run_check.sh --xvfb): hotkeys are fired by sending the
; combos with Send (XTEST events go through the X grab and are dispatched in
; the main loop / during MsgSleep waits).  Output goes to a file.
#Requires AutoHotkey v2.0

WINOUT := "/tmp/ahk_dc_hotkey_out.txt"
FileDelete(WINOUT)
Log(line) => FileAppend(line "`n", WINOUT)

; --- Simple hotkey fires when its key is pressed. ---
cnt1 := 0
CB1(ThisHotkey) {
    global cnt1
    cnt1++
}
Hotkey("F7", CB1)
Sleep(200) ; Let the grab be established.
Send("{F7}")
Sleep(300)
Log("hk_f7=" (cnt1 = 1 ? 1 : 0))

; --- Modifier combos: ^ (Ctrl), + (Shift), ! (Alt). ---
cnt2 := 0
CB2(ThisHotkey) {
    global cnt2
    cnt2++
}
Hotkey("^F8", CB2)
Sleep(200)
Send("^{F8}")
Sleep(300)
Log("hk_ctrl=" (cnt2 = 1 ? 1 : 0))
; An extraneous/different modifier must NOT fire the hotkey (docs: extra
; modifiers are not allowed by default).
cnt3 := 0
CB3(ThisHotkey) {
    global cnt3
    cnt3++
}
Hotkey("+F8", CB3)
Sleep(200)
Send("^{F8}")
Sleep(300)
Log("hk_mods_exact=" (cnt3 = 0 ? 1 : 0))
Send("+{F8}")
Sleep(300)
Log("hk_shift=" (cnt3 = 1 ? 1 : 0))
cnt5 := 0
CB5(ThisHotkey) {
    global cnt5
    cnt5++
}
Hotkey("!F6", CB5)
Sleep(200)
Send("!{F6}")
Sleep(300)
Log("hk_alt=" (cnt5 = 1 ? 1 : 0))

; --- Docs: "On"/"Off" actions enable/disable the hotkey. ---
Hotkey("F7", "Off")
Send("{F7}")
Sleep(300)
Log("hk_off=" (cnt1 = 1 ? 1 : 0))
Hotkey("F7", "On")
Send("{F7}")
Sleep(300)
Log("hk_on=" (cnt1 = 2 ? 1 : 0))

; --- Key-up hotkeys ("Key up") fire on release. ---
cnt4 := 0
CB4(ThisHotkey) {
    global cnt4
    cnt4++
}
Hotkey("F9 up", CB4)
Sleep(200)
Send("{F9}")
Sleep(300)
Log("hk_up=" (cnt4 = 1 ? 1 : 0))

; --- Invalid key name -> ValueError (docs). ---
try
    Hotkey("NoSuchKeyXYZ", CB1)
catch ValueError
    Log("hk_badkey=1")

; --- Hotkeys keep the script alive (docs: persistent); timer coexistence. ---
n := 0
T() {
    global n
    n++
}
SetTimer(T, 60)
cnt6 := 0
CB6(ThisHotkey) {
    global cnt6
    cnt6++
}
Hotkey("F10", CB6)
Sleep(200)
Send("{F10}")
Sleep(300)
Log("hk_with_timer=" (cnt6 = 1 && n >= 1 ? 1 : 0))

; --- A_ThisHotkey / A_PriorHotkey / A_EndChar tracking (check_detail0821 §5). ---
this_ok := 0
CB7(ThisHotkey) { ; F11 handler: A_ThisHotkey = F11, A_PriorHotkey = F10.
    global this_ok
    ; Prior hotkey is F10 (fired above); F10's cb ran in its own thread, so
    ; A_PriorHotkey should be "F10" when F11 fires.
    this_ok := (A_ThisHotkey = "F11" && A_PriorHotkey = "F10") ? 1 : 0
}
Hotkey("F11", CB7)
Sleep(200)
Send("{F11}")
Sleep(300)
Log("hk_this_prior=" this_ok)

; --- caps API: A_HotkeyBackend + HotkeyBackendGet (check_detail0821 §1-B/D / R3). ---
bk := A_HotkeyBackend
Log("caps_backend_nn=" (bk != "" ? 1 : 0))
bo := HotkeyBackendGet()
Log("caps_obj_ok=" (IsObject(bo) && bo.backend != "" && (bo.global_hotkeys = 1 || bo.global_hotkeys = 0) ? 1 : 0))

; --- Cleanup. ---
ExitApp(0)
