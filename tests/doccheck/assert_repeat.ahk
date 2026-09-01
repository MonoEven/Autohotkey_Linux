; Key auto-repeat / hold / aaaa regression (check0820 §2):
;   - "aaaa" typed text must produce four 'a' characters at the foreground
;     client (the capture/passthrough path must not swallow repeats);
;   - a hotkey receiving repeated KeyPress events (as a held key produces
;     through X server auto-repeat) must fire for EVERY repeat, not just
;     the first -- XTest re-injection for passthrough must not be confused
;     with a genuine repeat (mirrors assert_hotkey_pt's double-tap);
;   - tilde passthrough: each repeated genuine press must BOTH fire the
;     callback AND reach the foreground client.
; Runs under Xvfb (run_check.sh --xvfb) with xkeycap as independent client.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_repeat_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)
; Official SendLevel rule (check0901 P0-2): the auto-exec driver triggers own
; hotkeys with SendEvent-class input at level 1 (the Windows-golden path).
SendLevel(1)
SendMode("Event")

KCFILE := "/tmp/ahk_dc_keycap_repeat.txt"
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

; ---- (1) aaaa: SendText("aaaa") must reach the client as 4 chars
;      (repeated letters are not swallowed by the capture/passthrough
;      path). ---
SendText("aaaa")
Sleep(250)
Log("repeat_aaaa=" (downs(next_lines()) = "a,a,a,a" ? 1 : 0))

; ---- (2) a held key's auto-repeat stream: every repeat must trigger the
;      hotkey.  XTest emits no server-side auto repeat, so simulate the
;      device stream with close successive presses (30ms cadence ~= a fast
;      machine-gun repeat). ---
cnt := 0
CBA(ThisHotkey) {
    global cnt
    cnt++
}
Hotkey("F12", CBA)
Sleep(300)
Loop 5 {
    Send("{F12}")
    Sleep(30)
}
Sleep(400)
Log("repeat_hold_each=" (cnt = 5 ? 1 : 0))

; ---- (3) a single tilde (passthrough) hotkey: fires the callback and the
;      key reaches the foreground client.  Per the documented deviation
;      (check0818 P0-3) the re-injected down-copy of a single press may be
;      swallowed by the still-active passive grab, but the release always
;      passes; mirroring assert_hotkey_pt, "the key arrived" is determined
;      by {down|up} in the capture log. ---
cntb := 0
CBB(ThisHotkey) {
    global cntb
    cntb++
}
Hotkey("~F9", CBB)
Sleep(300)
Send("{F9}")
Sleep(400)
has_key := InStr(FileRead(KCFILE), "k:down:F9") || InStr(FileRead(KCFILE), "k:up:F9")
Log("repeat_single_tilde_cnt=" cntb)
Log("repeat_single_tilde_fg=" (has_key ? 1 : 0))

RunWait("pkill -x xkeycap")
ExitApp 0