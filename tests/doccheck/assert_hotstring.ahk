; Hotstring expansion doc-check (round 32): the port now expands
; hotstrings from the live typed-text stream (all-keys passive grabs while
; any hotstring is enabled).  Verified with the independent xkeycap client:
;   - "::xq" -> "ZZ" on the end char (space): the trigger never reaches the
;     client, the replacement does, and the end char follows;
;   - "O::cd" -> "Q": the end char is omitted;
;   - "X::ef" runs the callback instead of sending text;
;   - "*::ab" -> "W": no end char required (fires on the next char, which
;     is passed through);
;   - unmatched text ("hello ") passes through unchanged.
; Requires --xvfb (xkeycap window + XTEST via Send).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_hotstring_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)
cnt := 0
CB(ThisHotkey) {
    global cnt
    cnt++
}

KCFILE := "/tmp/ahk_dc_keycap_hs.txt"
FileDelete(KCFILE)
Run('out/xkeycap -out ' KCFILE ' -ms 30000')
WinWait("KeyCap Capture",, 5)
Sleep(300)

Hotstring("::xq", "ZZ")     ; end char required.
Hotstring(":O:cd", "Q")     ; omit the end char.
Hotstring(":X:ef", CB)      ; callback form.
Hotstring(":*:ab", "W")     ; no end char required.
Hotstring(":*:你好", "nn")  ; round-34: Unicode trigger (CJK).
Sleep(300)

Send("xq ")                 ; -> "ZZ" + space.
Send("cd ")                 ; -> "Q", space omitted.
Send("ef.")                 ; callback fires; '.' forwarded.
Send("abz")                 ; -> "W" + 'z'.
Send("hello ")              ; no match -> passthrough.
SendText("你好")             ; round-34: CJK trigger -> "cn".
Sleep(800)

; Callback result.
Log("hs_cb=" cnt)
; Let the last forwarded events reach the client before reading its log.
Sleep(500)
; Foreground client results (xkeycap keysym log).
kc := FileRead(KCFILE)
Log("hs_zz_seen=" (InStr(kc, "k:down:Z:") ? 1 : 0))       ; replacement sent.
Log("hs_x_hidden=" (InStr(kc, "k:down:x:", true) ? 0 : 1))     ; trigger suppressed.
Log("hs_q_hidden=" (InStr(kc, "k:down:q:", true) ? 0 : 1))
Log("hs_w_seen=" (InStr(kc, "k:down:W:") ? 1 : 0))       ; * option replacement.
Log("hs_a_hidden=" (InStr(kc, "k:down:a:", true) ? 0 : 1))
Log("hs_b_hidden=" (InStr(kc, "k:down:b:", true) ? 0 : 1))
Log("hs_c_hidden=" (InStr(kc, "k:down:c:", true) ? 0 : 1))
Log("hs_d_hidden=" (InStr(kc, "k:down:d:", true) ? 0 : 1))
Log("hs_hello=" (InStr(kc, "k:down:h:") ? 1 : 0))        ; unmatched text passes.
Log("hs_unicode=" (InStr(kc, "k:down:n:") ? 1 : 0))      ; CJK replacement (nn).
Log("hs_cjk_hidden=" ((InStr(kc, "U4F60", true) or InStr(kc, "U597D", true)) ? 0 : 1)) ; CJK trigger suppressed.
; The O option must omit exactly the "cd " end char: count spaces in
; the client log (xq's + hello's = 2).
sp := 0
spos := 1
loop {
    spos := InStr(kc, "k:down:space:", true, spos)
    if !spos
        break
    sp++
    spos += 13
}
Log("hs_space_count=" (sp = 2 ? 1 : 0))

; Release any keys the capture engine may have left physically down
; (their events were held toward a match; on exit they would stay down at
; the X server until the connection closes, briefly auto-repeating and
; polluting the next suite's key capture).
Send("{Space up}")
Send("{Shift up}")
Sleep(100)
; Clean up the foreground client.
RunWait("pkill -x xkeycap")
ExitApp 0
