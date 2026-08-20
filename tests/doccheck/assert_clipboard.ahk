; Clipboard regression suite (check0820 §2):
;   - multi-MIME TARGETS advertisement on the X11 CLIPBOARD selection
;     (UTF8_STRING/STRING advertised; image/png refused);
;   - a slow consumer that pulls the data AFTER the write call must still
;     get it (the owner keeps serving until the data is actually pulled);
;   - very large text (252 KiB) round-trips intact;
;   - reading from an external slow owner (xclip_probe --set) works;
;   - an empty clipboard reads back empty.
; Uses xclip_probe.c, an INDEPENDENT X11 client, so the owner and the
; consumer are different processes (not the engine testing itself).
; Runs under Xvfb (run_check.sh --xvfb).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_clip_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

PROBE := "out/xclip_probe"
TBASE := "/tmp/ahk_dc_probe"

; Run the probe (foreground), wait up to 3 s for its output file, read it.
probe_run(opts) {
    global TBASE
    FileDelete(TBASE ".txt")
    RunWait(PROBE ' ' opts ' -out ' TBASE '.txt')
    i := 0
    while !FileExist(TBASE ".txt") && i < 60 {
        Sleep(50)
        i += 1
    }
    t := FileExist(TBASE ".txt") ? FileRead(TBASE ".txt") : ""
    return RTrim(t, "`r`n")   ; probe appends a trailing newline
}

; --- write side: AHK takes CLIPBOARD ownership ---
A_Clipboard := "clip-mime-text"

; --- TARGETS: the owner must advertise UTF8_STRING/STRING (text) ---
Sleep(150)
t := probe_run("--targets")
Log("clip_targets_utf8=" (InStr(t, "UTF8_STRING") ? 1 : 0))
Log("clip_targets_string=" (InStr(t, "STRING") ? 1 : 0))
Log("clip_targets_has_targets=" (InStr(t, "TARGETS") ? 1 : 0))

; --- Non-text MIME is refused (no image/png or text/html service) ---
Sleep(100)
Log("clip_img_refused=" (probe_run("--ask-target image/png") = "none" ? 1 : 0))
Log("clip_html_refused=" (probe_run("--ask-target text/html") = "none" ? 1 : 0))

; --- Normal read-back (immediate consumer) ---
Sleep(100)
Log("clip_get_roundtrip=" (probe_run("--get") = "clip-mime-text" ? 1 : 0))

; --- Slow consumer: pulls 800 ms after the write; the data must still
;     be served (the engine must not drop the selection) ---
Sleep(100)
Log("clip_slow_consumer=" (probe_run("--get --delay 800") = "clip-mime-text" ? 1 : 0))

; --- Very large text (252 KiB) round-trips byte-exact ---
big := ""
Loop 4000
    big .= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!"  ; 63 B * 4000 = 252 KB
A_Clipboard := big
Sleep(150)
got := probe_run("--get")
Log("clip_big_len=" (StrLen(got) = StrLen(big) ? 1 : 0))
Log("clip_big_head=" (SubStr(got, 1, 20) = SubStr(big, 1, 20) ? 1 : 0))
Log("clip_big_tail=" (SubStr(got, -20) = SubStr(big, -20) ? 1 : 0))

; --- External slow owner: xclip_probe --set takes ownership and serves
;     for 3 s; AHK must read the text while the foreign owner is alive ---
FileDelete(TBASE "_own.txt")
Run(PROBE ' --set --delay 3000 -out ' TBASE '_own.txt')
i := 0
while !FileExist(TBASE "_own.txt") && i < 40 {
    Sleep(50)
    i += 1
}
Sleep(150)
ext := A_Clipboard
Log("clip_external_owner=" (ext = "probe-owner-data" ? 1 : 0))
RunWait("pkill -f xclip_probe")

; --- Empty clipboard reads back empty ---
A_Clipboard := ""
Sleep(150)
Log("clip_empty_read=" (A_Clipboard = "" ? 1 : 0))

; --- ClipWait returns immediately when text is present ---
A_Clipboard := "clip-wait-text"
Sleep(150)
t0 := A_TickCount
w := ClipWait(0.5)
Log("clip_clipwait_ok=" ((w = 1 && A_TickCount - t0 < 500) ? 1 : 0))

A_Clipboard := ""
Sleep(100)
ExitApp 0