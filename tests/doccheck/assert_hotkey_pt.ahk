; Hotkey passthrough / un-grab doc-check (check0818 P0-2/P0-3):
; verifies with an INDEPENDENT X11 client (xkeycap, which owns the input
; focus) that:
;   - an ordinary hotkey suppresses the key (foreground client does NOT see
;     it), while the callback fires;
;   - a tilde (~) hotkey fires AND the key reaches the foreground client;
;   - "Hotkey X, Off" ungrabs the combination (callback does not fire and
;     the key reaches the foreground client);
;   - a HotIf-false hotkey fires nothing and the key reaches the foreground
;     client (passthrough re-injection).
; Requires --xvfb (xkeycap window + XTEST via Send).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_hotkeypt_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)
cnt1 := 0
cnt2 := 0
cnt3 := 0
cnt4 := 0
CB1(ThisHotkey) {
    global cnt1
    cnt1++
}
CB2(ThisHotkey) {
    global cnt2
    cnt2++
}
CB3(ThisHotkey) {
    global cnt3
    cnt3++
}
CB4(ThisHotkey) {
    global cnt4
    cnt4++
}

KCFILE := "/tmp/ahk_dc_keycap_pt.txt"
FileDelete(KCFILE)
Run('out/xkeycap -out ' KCFILE ' -ms 30000')
WinWait("KeyCap Capture",, 5)
Sleep(300)

Hotkey("F7", CB1)          ; ordinary -> suppressed.
Hotkey("~F8", CB2)         ; tilde -> passthrough.
Hotkey("F9", CB3)
Hotkey("F9", "Off")        ; disabled -> ungrabbed.
HotCritF(c) {
    return false
}
HotIf(HotCritF)            ; condition false -> passthrough.
Hotkey("F10", CB4)
HotIf()                    ; reset.
Sleep(300)

Send("{F7}")
Send("{F8}")
Send("{F9}")
Send("{F10}")
Sleep(400)

; Callback results.
Log("pt_cnt1=" cnt1)
Log("pt_cnt2=" cnt2)
Log("pt_cnt3=" cnt3)
Log("pt_cnt4=" cnt4)
; Foreground client results (xkeycap keysym log).
kc := FileRead(KCFILE)
Log("pt_f7_hidden=" (InStr(kc, "F7") ? 0 : 1))      ; suppressed.
Log("pt_f8_seen=" (InStr(kc, "F8") ? 1 : 0))        ; tilde passthrough.
Log("pt_f9_seen=" (InStr(kc, "F9") ? 1 : 0))        ; Off ungrabbed.
Log("pt_f10_seen=" (InStr(kc, "F10") ? 1 : 0))      ; HotIf-false passthrough.

; Clean up the foreground client so later suites (PixelSearch etc.) are
; not disturbed by its window.
RunWait("pkill -x xkeycap")
ExitApp 0
