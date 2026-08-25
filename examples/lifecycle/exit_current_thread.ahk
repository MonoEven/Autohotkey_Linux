#Requires AutoHotkey v2.0
; Exit ends the current thread. In auto-execute this also lets the script end.
out := A_Args.Length ? A_Args[1] : "/tmp/ahk-example-exit.txt"
FileAppend("before-exit`n", out)
Exit 7
FileAppend("unreachable`n", out)
