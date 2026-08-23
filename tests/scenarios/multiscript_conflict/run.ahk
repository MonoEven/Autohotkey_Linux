#Requires AutoHotkey v2.0
; Scenario multiscript_conflict: a second process registering the same global
; hotkey must get a conflict error (not a silent no-op).
WINOUT := "/tmp/scn_multiscript_conflict"
FileDelete(WINOUT)
FileDelete("/tmp/scn_ms_holder_ready")
Run('"' A_AhkPath '" /tmp/scn_ms_holder.ahk')
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
conflict_seen := 0
try {
    Hotkey("F7", (*) => 0)
    FileAppend("noconflict`n", WINOUT)
} catch OSError {
    conflict_seen := 1
}
if (conflict_seen)
    FileAppend("conflict:`n", WINOUT)
ExitApp