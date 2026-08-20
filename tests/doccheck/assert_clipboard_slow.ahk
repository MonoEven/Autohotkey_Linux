; Clipboard slow-Owner regression (check0820 §2):
;   - a SLOW app holds CLIPBOARD ownership and only serves the reader's
;     SelectionRequest after 2.5 s (xclip_probe --set --serve-delay 2500);
;   - the in-process read with the DEFAULT AHK_CLIPBOARD_TIMEOUT_MS=2000
;     must time out cleanly (A_Clipboard empty, elapsed ~2 s, no hang);
;   - a CHILD AHK run with AHK_CLIPBOARD_TIMEOUT_MS=5000 must succeed and
;     return the owner's text (the env knob raises the bounded wait).
; Runs under Xvfb (run_check.sh --xvfb).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_clip_slow_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

PROBE := "out/xclip_probe"
TBASE := "/tmp/ahk_dc_probe_slow"

; Launch the slow owner: owns CLIPBOARD for 7 s, serves each request only
; after 2.5 s.  Wait for its "ready" marker (ownership taken).
FileDelete(TBASE ".own.txt")
Run(PROBE ' --set --delay 7000 --serve-delay 2500 -out ' TBASE '.own.txt')
i := 0
while !FileExist(TBASE ".own.txt") && i < 60 {
    Sleep(50)
    i += 1
}
ready := FileExist(TBASE ".own.txt") ? FileRead(TBASE ".own.txt") : ""
Log("slow_owner_ready=" (InStr(ready, "probe-owner-ready") ? 1 : 0))
Sleep(200)

; --- In-process read, default timeout (2 s < 2.5 s serve delay) ---
t0 := A_TickCount
got_default := A_Clipboard
elapsed_default := A_TickCount - t0
Log("slow_default_text=" (got_default = "probe-owner-data" ? 1 : 0))
Log("slow_default_timedout=" (got_default = "" ? 1 : 0))
Log("slow_default_elapsed_ms=" elapsed_default)

; --- Child AHK with the raised timeout (5 s >= 2.5 s serve delay) ---
child_ahk := "/tmp/ahk_dc_clip_slow_child.ahk"
FileDelete("/tmp/ahk_dc_clip_slow_child_out.txt")
FileDelete(child_ahk)
; The child is written with SINGLE-quoted AHK literals so no double quotes
; need escaping inside the parent's double-quoted source (AHK v2 typifies
; backtick, not "" doubling).
FileAppend(
  "got := A_Clipboard`n"
  "FileAppend('child_text=' (got = 'probe-owner-data' ? 1 : 0), '/tmp/ahk_dc_clip_slow_child_out.txt')`n"
  "ExitApp 0`n", child_ahk)
; The child is the SAME interpreter (A_AhkPath), re-run with the env knob
; raised.  bash -c lets us set the variable without mutating our own env.
RunWait('bash -c "AHK_CLIPBOARD_TIMEOUT_MS=5000 ' A_AhkPath ' ' child_ahk '"', , "Hide")
i := 0
while !FileExist("/tmp/ahk_dc_clip_slow_child_out.txt") && i < 80 {
    Sleep(50)
    i += 1
}
child := FileExist("/tmp/ahk_dc_clip_slow_child_out.txt") ? FileRead("/tmp/ahk_dc_clip_slow_child_out.txt") : ""
Log("slow_env_child=" (InStr(child, "child_text=1") ? 1 : 0))

RunWait("pkill -f xclip_probe")
ExitApp 0