#Requires AutoHotkey v2.0
counter := 0
while counter < 1000000000
    counter += 1
FileAppend("unexpected-completion`n", "/tmp/ahk-dbgp-running.out")
