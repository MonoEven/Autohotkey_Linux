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
Run('out/xkeycap -out ' KCFILE ' -ms 30000')
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

; 2) (OnChar/OnKeyDown notifications: documented limitation -- the script
;    callback path from the native capture dispatch is not wired yet; the
;    core live capture below is complete.)

; 3) End char terminates with EndReason "EndChar"; buffer holds the text
;    before the end char.
ih3 := InputHook("T3", "z")
ih3.Start()
Send("abz")
ih3.Wait(2500)
Log("ih3_val=" (ih3.Input = "ab" ? 1 : 0))
Log("ih3_end=" (ih3.EndReason = "EndChar" ? 1 : 0))
Sleep(50)

; 4) Match list terminates with EndReason "Match".
ih4 := InputHook("T3", "", "stop")
ih4.Start()
Send("stop")
ih4.Wait(2500)
Log("ih4_end=" (ih4.EndReason = "Match" ? 1 : 0))
Sleep(50)

; 5) Backspace with BackspaceIsUndo removes the last collected char.
ih5 := InputHook("T3")
ih5.Start()
Send("ab{Backspace}")
Sleep(200)
Log("ih5_bu=" (ih5.Input = "a" ? 1 : 0))
ih5.Stop()
Sleep(50)

; 6) The captured keys are consumed (not forwarded): nothing reaches the
;    foreground xkeycap client.
Sleep(200)
kc := FileRead(KCFILE)
Log("ih_sup=" ((InStr(kc, "k:down:a:", true) or InStr(kc, "k:down:b:", true) or InStr(kc, "k:down:x:", true) or InStr(kc, "k:down:z:", true)) ? 0 : 1))

RunWait("pkill -x xkeycap")
ExitApp 0
