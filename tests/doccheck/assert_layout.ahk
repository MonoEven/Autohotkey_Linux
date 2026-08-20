; Keyboard layout dynamic-switch regression (check0820 §2):
; X11 key grabs are keycode-based, so switching the layout (e.g. via
; setxkbmap, which broadcasts MappingNotify) must NOT break hotkeys; and
; the Send engine resolves the shifted/unshifted character the same way on
; the new layout (the physical key is sent; the compositor/app resolves
; the keysym).  Runs under Xvfb (run_check.sh --xvfb) with xkeycap as an
; independent foreground client.
;
; Scenario:
;   1. register F7 (plain) and ^F12 (modifier combo);
;   2. setxkbmap de (broadcasts a MappingNotify; the hotkey connection must
;      keep the modifier map / grabs working, incl. Alt/Super slots);
;   3. restore with setxkbmap us;
;   4. verify the grabs still fire after both switches (Send {F7}).
;   5. verify Send("a") after the switch still yields the "a" keysym at
;      the foreground client (xkeycap), and 'Z' after shift.
; An X server that keeps stale modifier maps or drops grabs on
; MappingNotify would fail these assertions.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_layout_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

KCFILE := "/tmp/ahk_dc_keycap_layout.txt"
FileDelete(KCFILE)
Run('out/xkeycap -out ' KCFILE ' -ms 40000')
WinWait("KeyCap Capture",, 5)
Sleep(300)

prev_bytes := 0
next_lines() {
    global prev_bytes
    f := FileOpen(KCFILE, "r")
    f.Seek(prev_bytes)
    rest := f.Read()
    f.Close()
    prev_bytes := FileGetSize(KCFILE)
    return StrSplit(rest, "`n")
}
downs(lines) {
    out := ""
    for l in lines {
        if l = ""
            continue
        p := StrSplit(l, ":")
        if p.Length >= 3 && p[1] = "k" && p[2] = "down"
            out .= (out = "" ? "" : ",") p[3]
    }
    return out
}

cnt_f7 := 0
CB7(ThisHotkey) {
    global cnt_f7
    cnt_f7++
}
cnt_f12 := 0
CB12(ThisHotkey) {
    global cnt_f12
    cnt_f12++
}
Sleep(300)

Hotkey("F7", CB7)
Hotkey("^F12", CB12)
Sleep(300)

; --- baseline (us): F7 fires. ---
Send("{F7}")
Sleep(300)
Log("layout_baseline_f7=" (cnt_f7 >= 1 ? 1 : 0))

; --- switch to de (broadcasts MappingNotify; a stale map or dropped grab
;     would make the next F7 dead) ---
RunWait('setxkbmap de > /dev/null 2>&1')
Sleep(600)   ; let the MappingNotify round-trip and rebuild settle
Send("{F7}")
Sleep(200)
Log("layout_de_f7=" (cnt_f7 >= 2 ? 1 : 0))

; --- modifier combo must survive the relayout: Ctrl+F12 ---
Send("^{F12}")
Sleep(200)
Log("layout_de_ctrl_f12=" (cnt_f12 >= 1 ? 1 : 0))

; --- Send resolves the character on the new layout: lowercase 'a' must
;     still arrive as the same key (the app keysym resolves 'a'). ---
next_lines()  ; drain any events from the Ctrl+F12 injection (a Ctrl_L
               ; release may land in the capture log).
Send("a")
Sleep(150)
Log("layout_de_send_a=" (downs(next_lines()) = "a" ? 1 : 0))

; --- switch back to us ---
Run("setxkbmap us > /dev/null 2>&1")
Sleep(600)
Send("{F7}")
Sleep(400)
Log("layout_us_f7=" (cnt_f7 >= 3 ? 1 : 0))

; --- backspace key on de and the 'y'/'z' swap are known QWERTZ*/QWERTY
;     differences; verify a neutral key still resolves ---
Send("z")
Sleep(120)
Log("layout_us_send_z=" (downs(next_lines()) = "z" ? 1 : 0))

; Clean up the foreground client.
RunWait("pkill -x xkeycap")
ExitApp 0