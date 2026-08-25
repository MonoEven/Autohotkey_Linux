#Requires AutoHotkey v2.0
; Reload this script exactly once and prove the replacement process reached auto-execute.
marker := A_Args.Length ? A_Args[1] : "/tmp/ahk-example-reload.marker"
out := A_Args.Length >= 2 ? A_Args[2] : "/tmp/ahk-example-reload.txt"
if !FileExist(marker) {
    FileAppend("first-instance`n", out)
    FileAppend("armed`n", marker)
    Reload
    ; Linux Reload returns after spawning the replacement; terminate the old
    ; auto-execute thread immediately so it cannot consume the marker.
    ExitApp
}
FileDelete(marker)
FileAppend("replacement-instance`n", out)
ExitApp
