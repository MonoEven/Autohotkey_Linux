; InputHook live key capture doc-check (round 33): while an InputHook is
; InProgress, the typed-text capture engine (all-keys passive grabs on the
; dedicated hotkey connection) feeds keys to the hook -- buffer fill, end
; chars, match list, buffer limit, backspace undo -- and consumes them (not
; forwarded), matching Windows Input.  Verified with the independent
; xkeycap client.  Requires --xvfb.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_inputhook_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

KCFILE := "/tmp/ahk_dc_keycap_ih.txt"
FileDelete(KCFILE)
Run('out/xkeycap -out ' KCFILE ' -ms 300000')
WinWait("KeyCap Capture",, 5)
Sleep(300)

; 1) Buffer fills with typed text while the hook runs.
ih := InputHook("T2")
ih.Start()
Send("a")
Send("b")
Sleep(200)
Log("ih_ab=" (ih.Input = "ab" ? 1 : 0))
ih.Stop()
Sleep(50)

; 2) OnChar/OnKeyDown/OnKeyUp notifications (round-34): queued by the capture
;    engine and fired from the main-loop dispatch (Windows semantics: VK/SC
;    for key events, the character for OnChar).  Named callbacks + globals:
;    v2 closures capture locals by value, so arrow functions cannot see the
;    main thread's updates.
ih2 := InputHook("T2")
seq := ""
kd := ""
ku := 0
ih2_OnChar(h, c) {
    global seq
    seq .= c
}
ih2_OnDown(h, vk, sc) {
    global kd
    kd .= vk "," sc ";"
}
ih2_OnUp(h, vk, sc) {
    global ku
    ku := ku + 1
}
ih2.OnChar := ih2_OnChar
ih2.OnKeyDown := ih2_OnDown
ih2.OnKeyUp := ih2_OnUp
ih2.Start()
Send("a")
Sleep(300)
Log("ih2_char=" (seq = "a" ? 1 : 0))
Log("ih2_kd=" (RegExMatch(kd, "^65,") ? 1 : 0))
Log("ih2_ku=" (ku >= 1 ? 1 : 0))
ih2.Stop()
Sleep(50)

; 3) Unicode characters flow through OnChar (round-34): SendText injects CJK
;    keysyms through the borrowed-keycode path; the capture engine converts
;    the Unicode keysym back to the character.
ih2b := InputHook("T3")
seq3 := ""
ih2b_OnChar(h, c) {
    global seq3
    seq3 .= c
}
ih2b.OnChar := ih2b_OnChar
ih2b.Start()
SendText("你好")
Sleep(300)
Log("ih3_unicode=" (seq3 = "你好" ? 1 : 0))
ih2b.Stop()
Sleep(50)

; 4) End char terminates with EndReason "EndChar"; buffer holds the text
;    before the end char.
ih3 := InputHook("T3", "z")
ih3.Start()
Send("abz")
ih3.Wait(2500)
Log("ih3_val=" (ih3.Input = "ab" ? 1 : 0))
Log("ih3_end=" (ih3.EndReason = "EndChar" ? 1 : 0))
Sleep(50)

; 5) Match list terminates with EndReason "Match".
ih4 := InputHook("T3", "", "stop")
ih4.Start()
Send("stop")
ih4.Wait(2500)
Log("ih4_end=" (ih4.EndReason = "Match" ? 1 : 0))
Sleep(50)

; 6) Backspace with BackspaceIsUndo removes the last collected char.
ih5 := InputHook("T3")
ih5.Start()
Send("ab{Backspace}")
Sleep(200)
Log("ih5_bu=" (ih5.Input = "a" ? 1 : 0))
ih5.Stop()
Sleep(50)

; 7) The captured keys are consumed (not forwarded): nothing reaches the
;    foreground xkeycap client.
Sleep(200)
kc := FileRead(KCFILE)
Log("ih_sup=" ((InStr(kc, "k:down:a:", true) or InStr(kc, "k:down:b:", true) or InStr(kc, "k:down:x:", true) or InStr(kc, "k:down:z:", true)) ? 0 : 1))

; Robust teardown: SIGKILL xkeycap (a stuck client can ignore SIGTERM and a
; blocking RunWait then wedges the runner; a bounded non-waiting kill plus an
; explicit ExitApp never does).
Run('pkill -9 -x xkeycap')
ExitApp 0
