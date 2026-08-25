#Requires AutoHotkey v2.0
; Reload this script exactly once and prove the replacement process reached auto-execute.
marker := A_Args.Length ? A_Args[1] : "/tmp/ahk-example-reload.marker"
out := A_Args.Length >= 2 ? A_Args[2] : "/tmp/ahk-example-reload.txt"
if !FileExist(marker) {
    FileAppend("first-instance`n", out)
    FileAppend("armed`n", marker)
    Reload
    ; The replacement starts asynchronously and then signals this process.
    ; Keep the old instance alive until that hand-off; reaching the line after
    ; Sleep means reload failed and is an explicit example failure.
    Sleep(3000)
    FileAppend("old-continued`n", out)
    ExitApp(9)
}
FileDelete(marker)
FileAppend("replacement-instance`n", out)
ExitApp
