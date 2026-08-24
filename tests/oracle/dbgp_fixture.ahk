#Requires AutoHotkey v2.0
x := 10
y := x + 5
z := y * 2
FileAppend("value=" z "`n", "/tmp/ahk-dbgp-fixture.out")
ExitApp
