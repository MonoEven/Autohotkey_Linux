; Mouse hotkey doc-check (XGrabButton backend): verifies with the
; independent xkeycap client (which logs button events delivered to its
; window) that:
;   - an ordinary mouse hotkey suppresses the button press (xkeycap does
;     NOT see it) while the callback fires;
;   - a tilde (~) mouse hotkey fires AND the click reaches xkeycap;
;   - a modifier mouse hotkey (^MButton) fires and suppresses;
;   - WheelUp fires and suppresses the scroll event;
;   - "Hotkey X, Off" ungrabs the button (xkeycap sees the click);
;   - a HotIf-false mouse hotkey fires nothing and the click reaches
;     xkeycap (passthrough re-injection).
; Requires --xvfb (xkeycap window + XTEST via Send/{Click}).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_hotkeybtn_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)
cnt1 := 0
cnt2 := 0
cnt3 := 0
cnt4 := 0
cnt5 := 0
cnt6 := 0
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
CB5(ThisHotkey) {
    global cnt5
    cnt5++
}
CB6(ThisHotkey) {
    global cnt6
    cnt6++
}

KCFILE := "/tmp/ahk_dc_keycap_btn.txt"
FileDelete(KCFILE)
Run('out/xkeycap -out ' KCFILE ' -ms 30000')
WinWait("KeyCap Capture",, 5)
Sleep(300)

Hotkey("LButton", CB1)      ; ordinary -> suppressed.
Hotkey("~RButton", CB2)     ; tilde -> passthrough.
Hotkey("^MButton", CB3)     ; modifier combo -> suppressed.
Hotkey("WheelUp", CB4)      ; wheel -> suppressed.
Hotkey("XButton1", CB5)
Hotkey("XButton1", "Off")   ; disabled -> ungrabbed.
HotCritF(c) {
    return false
}
HotIf(HotCritF)             ; condition false -> passthrough.
Hotkey("XButton2", CB6)
HotIf()                     ; reset.
Sleep(300)

; Position the pointer over the xkeycap window (0,0 300x200) and inject.
Send("{Click 150 100 Left}")   ; activates the LButton grab.
Send("{Click Right}")          ; ~RButton.
Send("^{Click Middle}")        ; ^MButton.
Send("{Click WheelUp}")        ; WheelUp.
Send("{Click X1}")             ; ungrabbed -> xkeycap sees it.
Send("{Click X2}")             ; HotIf false -> passthrough.
Sleep(400)

; Callback results.
Log("btn_cnt1=" cnt1)
Log("btn_cnt2=" cnt2)
Log("btn_cnt3=" cnt3)
Log("btn_cnt4=" cnt4)
Log("btn_cnt5=" cnt5)
Log("btn_cnt6=" cnt6)
; Foreground client results (xkeycap button log).
kc := FileRead(KCFILE)
Log("btn_l_hidden=" (InStr(kc, "b:down:1:") ? 0 : 1))   ; suppressed.
Log("btn_r_seen=" (InStr(kc, "b:down:3:") ? 1 : 0))     ; tilde passthrough.
Log("btn_m_hidden=" (InStr(kc, "b:down:2:") ? 0 : 1))   ; ^MButton suppressed.
Log("btn_w_hidden=" (InStr(kc, "b:down:4:") ? 0 : 1))   ; wheel suppressed.
Log("btn_x1_seen=" (InStr(kc, "b:down:8:") ? 1 : 0))    ; Off ungrabbed.
Log("btn_x2_seen=" (InStr(kc, "b:down:9:") ? 1 : 0))    ; HotIf-false passthrough.

; Clean up the foreground client.
RunWait("pkill -x xkeycap")
ExitApp 0
