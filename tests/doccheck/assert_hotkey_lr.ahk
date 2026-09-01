; Left/right modifier + wildcard modifier doc-check (check0818 batch 2):
; verifies with the independent xkeycap client that:
;   - <^a (LCtrl+a) fires for LCtrl+a and suppresses it; >^a fires for
;     RCtrl+a; neither fires for the wrong side;
;   - a wrong-side press that no variant matches is passed through to the
;     foreground client (xkeycap sees the key), like Windows;
;   - *F6 (wildcard) fires with no modifiers and with ^+! held;
;   - ^b (exact) beats *^b (wildcard) for Ctrl+b, and *^b fires alone for
;     Ctrl+Shift+b.
; Requires --xvfb (xkeycap window + XTEST via Send).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_hotkeylr_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)
; Official SendLevel rule (check0901 P0-2): drive own hotkeys with
; SendEvent-class input at level 1 (the Windows-golden trigger path).
SendLevel(1)
SendMode("Event")
cnt1 := 0
cnt2 := 0
cnt3 := 0
cnt4 := 0
cnt5 := 0
cnt7 := 0
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
CB7(ThisHotkey) {
    global cnt7
    cnt7++
}

KCFILE := "/tmp/ahk_dc_keycap_lr.txt"
FileDelete(KCFILE)
Run('out/xkeycap -out ' KCFILE ' -ms 30000')
WinWait("KeyCap Capture",, 5)
Sleep(300)

Hotkey("<^a", CB1)      ; LCtrl+a only.
Hotkey(">^a", CB2)      ; RCtrl+a only.
Hotkey("<^c", CB7)      ; LCtrl+c only; RCtrl+c must pass through.
Hotkey("*F6", CB3)      ; wildcard: fires with any/no modifiers.
Hotkey("^b", CB4)       ; exact.
Hotkey("*^b", CB5)      ; wildcard superset of ^b.
Sleep(300)

Send("^a")              ; LCtrl+a -> CB1, suppressed.
Send("{RCtrl down}")
Send("a")               ; RCtrl+a -> CB2, suppressed.
Send("{RCtrl up}")
Send("{RCtrl down}")
Send("c")               ; RCtrl+c -> no match, passed through.
Send("{RCtrl up}")
Send("{F6}")            ; wildcard, no modifiers -> CB3.
Send("^+!{F6}")         ; wildcard, Ctrl+Shift+Alt -> CB3.
Send("^b")              ; exact ^b wins over wildcard.
Send("^+{b}")           ; wildcard *^b only.
Sleep(400)

; Callback results.
Log("lr_cnt1=" cnt1)
Log("lr_cnt2=" cnt2)
Log("lr_cnt7=" cnt7)
Log("lr_cnt3=" cnt3)
Log("lr_cnt4=" cnt4)
Log("lr_cnt5=" cnt5)
; Foreground client results (xkeycap keysym log).
kc := FileRead(KCFILE)
Log("lr_a_hidden=" (InStr(kc, "k:down:a:") ? 0 : 1))     ; both ctrl+a suppressed.
Log("lr_c_passthru=" (InStr(kc, "k:down:c:") ? 1 : 0))   ; wrong-side c passed through.
Log("lr_b_hidden=" (InStr(kc, "k:down:b:") ? 0 : 1))     ; both ^b suppressed.
Log("lr_f6_hidden=" (InStr(kc, "k:down:F6:") ? 0 : 1))   ; wildcard F6 suppressed.

; Clean up the foreground client.
RunWait("pkill -x xkeycap")
ExitApp 0
