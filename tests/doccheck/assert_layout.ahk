; Layout-aware three-layer key model regression (check_detail0824 M1-K).
; A deterministic <=255-keycode XKB fixture is used because current distro
; xkeyboard-config maps exceed X11's keycode limit and xkbcomp may otherwise
; clip/reject them while leaving Xvfb on US (the former test was a false oracle).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_layout_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)
; Official SendLevel rule (check0901 P0-2): drive own hotkeys with
; SendEvent-class input at level 1 (the Windows-golden trigger path).
SendLevel(1)
SendMode("Event")
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

orig := "/tmp/ahk_dc_layout_original.xkb"
fixture := A_ScriptDir "/../oracle/azerty-altgr-test.xkb"
display := EnvGet("DISPLAY")
RunWait("xkbcomp -xkb " display " " orig)

cnt_f7 := 0
CB7(*) {
    global cnt_f7
    cnt_f7++
}
cnt_f12 := 0
CB12(*) {
    global cnt_f12
    cnt_f12++
}
Hotkey("F7", CB7)
Hotkey("^F12", CB12)
Sleep(300)

Send("{F7}")
Sleep(300)
Log("layout_baseline_f7=" (cnt_f7 >= 1 ? 1 : 0))

; Load a real server keymap: physical KEY_A now resolves to q; logical a is on
; another key and EuroSign is available only through Mod5/AltGr.
RunWait("xkbcomp -w 0 " fixture " " display)
Sleep(600)
Send("{F7}")
Sleep(200)
Log("layout_azerty_f7=" (cnt_f7 >= 2 ? 1 : 0))
Send("^{F12}")
Sleep(200)
Log("layout_azerty_ctrl_f12=" (cnt_f12 >= 1 ? 1 : 0))

next_lines() ; drain hotkey modifier events
Send("a")
Sleep(200)
Log("layout_azerty_send_a=" (downs(next_lines()) = "a" ? 1 : 0))
RunWait("xkbcomp -w 0 " orig " " display)
Sleep(600)
Send("{F7}")
Sleep(400)
Log("layout_restore_f7=" (cnt_f7 >= 3 ? 1 : 0))
Send("z")
Sleep(120)
Log("layout_restore_send_z=" (downs(next_lines()) = "z" ? 1 : 0))

RunWait("pkill -x xkeycap")
ExitApp 0
