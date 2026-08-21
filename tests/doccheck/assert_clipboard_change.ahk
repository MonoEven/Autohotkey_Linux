; Clipboard-change notification regression (check_detail0821 §4 / R1-1).
;
; OnClipboardChange must fire when the X11 CLIPBOARD selection changes
; externally, with the Windows Type argument:
;   Type=1 for text writes, Type=0 when the owner vanishes (clipboard empty).
; Previously the AddClipboardFormatListener/... shims were no-ops, so the
; callback never fired at all.
;
; Drives the external events itself with xclip (an independent client)
; under Xvfb (run via run_check.sh --xvfb; same DISPLAY):
;   Stage A: self-write (A_Clipboard :=) -> Type 1
;   Stage B: external xclip write      -> Type 1
;   Stage C: kill the owner process    -> Type 0 (connection closed)
#Requires AutoHotkey v2.0
Persistent

OUT := "/tmp/ahk_dc_clip_change_out.txt"
CBFILE := "/tmp/ahk_dc_clip_change_cb.txt"
FileDelete(OUT)
FileDelete(CBFILE)
Log(line) => FileAppend(line "`n", OUT)

count1 := 0
count0 := 0
OnClipboardChange((type, *) => FileAppend("cb-" type "`n", CBFILE))
Log("ready=1")

; Stage A: the engine itself takes ownership (self-change also fires on
; Windows; the callback records it).
A_Clipboard := "self-change-text"
Sleep 700

; Stage B: an unrelated process (xclip) takes ownership with text.
Run('sh -c "echo external-change-text | xclip -selection clipboard"', , "Hide")
Sleep 900

; Stage C: the owner dies -> connection close -> clipboard empty.
Run("pkill xclip", , "Hide")
Sleep 900

; Tally the recorded callbacks.
For line in StrSplit(FileRead(CBFILE), "`n")
{
    if (line = "cb-1")
        count1++
    else if (line = "cb-0")
        count0++
}
Log("change_text_ok=" (count1 >= 1 ? 1 : 0))
Log("change_text_count=" count1)
Log("change_clear_ok=" (count0 >= 1 ? 1 : 0))
Log("change_clear_count=" count0)
Log("change_total=" (count1 + count0))
Log("change_total_ge2=" ((count1 + count0) >= 2 ? 1 : 0))

ExitApp 0