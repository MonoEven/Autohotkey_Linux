#Requires AutoHotkey v2.0
x := 10, arr := [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20], obj := {alpha: "A", nested: {beta: 42}}, mapv := Map("first", 101, "second", 202)
y := x + 5
z := y * 2
FileAppend("value=" z "`n", "/tmp/ahk-dbgp-fixture.out")
ExitApp
