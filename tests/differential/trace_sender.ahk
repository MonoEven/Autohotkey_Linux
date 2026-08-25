#Requires AutoHotkey v2.0
#SingleInstance Off
Sleep(300)
SendLevel(1)
switch A_Args[1] {
case "input":
    SendEvent("a+A{F13}")
case "hotkeys":
    SendEvent("^{F11}{F12}")
case "hotstring":
    SendEvent("zxq")
case "wildcard":
    SendEvent("+{F10}")
case "case-hotstring":
    SendEvent("zxc zXc")
case "inside-hotstring":
    SendEvent("xnom xyes")
case "end-hotstring":
    SendEvent(" omx ")
default:
    ExitApp(2)
}
ExitApp
