; Hotstring expansion doc-check (M2-R): XI2.1 raw observation never grabs or
; replaces physical events with XSendEvent. The original trigger reaches the
; independent xkeycap client (syn:0), then AHK sends the documented Backspace
; count + replacement. O/*/X/case/Unicode semantics remain covered.
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
Hotstring(":*:你好", "nn")  ; Unicode trigger (CJK).
Hotstring(":B0*:gh", "R")   ; B0: do not erase original trigger.
Hotstring("::ij", "S")      ; default: must not match inside a word.
Sleep(300)

; Synthetic test input needs level > the default Hotstring input level 0.
; Auto-replacement itself is forced to level 0 and cannot recurse.
SendLevel(1)
Send("xq ")                 ; -> "ZZ" + space.
Send("cd ")                 ; -> "Q", space omitted.
Send("ef.")                 ; callback fires; '.' forwarded.
Send("abz")                 ; -> "W" + 'z'.
Send("hello ")              ; no match -> passthrough.
SendText("你好")             ; CJK raw trigger -> Backspace x2 + "nn".
Send("gh")                   ; B0 -> original "gh" remains, then R.
Send("xij ")                ; inside-word guard: no replacement.
Send(" ij ")                ; boundary match: Backspace x3 + S + space.
SendLevel(0)
Sleep(800)

; Callback result + caps behavior contract: a backend may advertise
; char_stream only when a real Hotstring fired on its character stream.
Log("hs_cb=" cnt)
Log("hs_caps_contract=" (HotkeyBackendGet().char_stream = 1 && cnt = 1 ? 1 : 0))
; Let the last forwarded events reach the client before reading its log.
Sleep(500)
; Foreground client results (xkeycap keysym log).
kc := FileRead(KCFILE)
Log("hs_zz_seen=" (InStr(kc, "k:down:Z:") ? 1 : 0))
Log("hs_raw_trigger=" (InStr(kc, "k:down:x:") && InStr(kc, "k:down:q:") ? 1 : 0))
Log("hs_no_xsendevent=" (InStr(kc, ":syn:1") ? 0 : 1))
Log("hs_w_seen=" (InStr(kc, "k:down:W:") ? 1 : 0))
Log("hs_a_seen=" (InStr(kc, "k:down:a:") ? 1 : 0))
Log("hs_b_seen=" (InStr(kc, "k:down:b:") ? 1 : 0))
Log("hs_c_seen=" (InStr(kc, "k:down:c:") ? 1 : 0))
Log("hs_d_seen=" (InStr(kc, "k:down:d:") ? 1 : 0))
Log("hs_hello=" (InStr(kc, "k:down:h:") ? 1 : 0))
Log("hs_unicode=" (InStr(kc, "k:down:n:") ? 1 : 0))
Log("hs_cjk_seen=" ((InStr(kc, "U4F60", true) && InStr(kc, "U597D", true)) ? 1 : 0))
Log("hs_b0=" (InStr(kc, "k:down:g:") && InStr(kc, "k:down:R:") ? 1 : 0))
s_count := 0
s_pos := 1
loop {
    s_pos := InStr(kc, "k:down:S:", true, s_pos)
    if !s_pos
        break
    s_count++
    s_pos += 9
}
Log("hs_inside_word=" (s_count = 1 ? 1 : 0))
; Raw model: original spaces xq/cd/hello (3) plus xq's restored end char (1).
; O omits cd's restored end char.
sp := 0
spos := 1
loop {
    spos := InStr(kc, "k:down:space:", true, spos)
    if !spos
        break
    sp++
    spos += 13
}
Log("hs_space_count=" (sp = 8 ? 1 : 0))
bs := 0
bpos := 1
loop {
    bpos := InStr(kc, "k:down:BackSpace:", true, bpos)
    if !bpos
        break
    bs++
    bpos += 18
}
Log("hs_backspace_count=" (bs = 16 ? 1 : 0))

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
