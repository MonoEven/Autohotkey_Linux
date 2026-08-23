#Requires AutoHotkey v2.0
; The second process must first receive BadAccess, then automatically acquire
; the still-registered hotkey after the holder exits (check_detail0824 §7-E1).
WINOUT := "/tmp/scn_multiscript_conflict"
FileDelete(WINOUT)
FileDelete("/tmp/scn_ms_holder_ready")
holder := A_ScriptDir "/holder.ahk"
holderPid := 0
Run('"' A_AhkPath '" "' holder '"',,, &holderPid)
; Wait until the holder signals its grab is registered (up to 10s).
deadline := A_TickCount + 10000
while (!FileExist("/tmp/scn_ms_holder_ready") && A_TickCount < deadline)
    Sleep 100
if (!FileExist("/tmp/scn_ms_holder_ready"))
{
    FileAppend("holder_not_ready`n", WINOUT)
    ExitApp
}
Sleep 200
fired := 0
OnF7(*) {
    global fired
    fired++
}
conflict_seen := 0
try {
    Hotkey("F7", OnF7)
    FileAppend("noconflict`n", WINOUT)
} catch OSError {
    conflict_seen := 1
}
if (conflict_seen)
    FileAppend("conflict:`n", WINOUT)
; Hotkey() has retained the desired registration. Once the holder closes,
; the dispatch loop's conflicted-grab retry must acquire it without a second
; Hotkey() call or process restart.
ProcessWaitClose(holderPid, 10)
Sleep 1800
Send("{F7}")
Sleep 500
FileAppend("recovered=" (fired = 1 ? 1 : 0) "`n", WINOUT)
ExitApp
