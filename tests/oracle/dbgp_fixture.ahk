#Requires AutoHotkey v2.0
x := 10, arr := [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20], obj := {alpha: "A", nested: {beta: 42}}, mapv := Map("first", 101, "second", 202), comProxy := ComObject("org.freedesktop.DBus"), typedScalar := ComValue(3, 42)
y := x + 5
z := y * 2
try
    throw Error("D3-boom")
catch as caught
    caughtMessage := caught.Message
idleValue := 77
FileAppend("value=" z " caught=" caughtMessage "`n", "/tmp/ahk-dbgp-fixture.out")
Persistent(True)
