#Requires AutoHotkey v2.0
; SAFETY BOUNDARY: the default run never shuts down or logs off the machine.
; Pass exactly --i-understand-logoff to exercise Shutdown 0 (log off).
; Other flags can power off/reboot and are intentionally not accepted here.
out := A_Args.Length >= 2 ? A_Args[2] : "/tmp/ahk-example-shutdown-refusal.txt"
if A_Args.Length < 1 || A_Args[1] != "--i-understand-logoff" {
    FileAppend("Refused: rerun with --i-understand-logoff only after saving work.`n", out)
    ExitApp(2)
}
Shutdown 0
