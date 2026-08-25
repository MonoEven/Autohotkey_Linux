#Requires AutoHotkey v2.0
#SingleInstance Off
global TracePath := A_Args[1]
global TraceSeq := 0
if FileExist(TracePath)
    FileDelete(TracePath)
JsonString(value) {
    value := StrReplace(value, "\\", "\\\\")
    value := StrReplace(value, '"', '\"')
    value := StrReplace(value, "`r", "\\r")
    value := StrReplace(value, "`n", "\\n")
    return '"' value '"'
}
Emit(caseName, kind, vk := 0, sc := 0, text := 0, name := "") {
    global TracePath, TraceSeq
    TraceSeq += 1
    FileAppend('{"schema":1,"seq":' TraceSeq ',"case":' JsonString(caseName)
        ',"kind":' JsonString(kind) ',"vk":' vk ',"sc":' sc ',"text":' text
        ',"name":' JsonString(name) '}' "`n", TracePath)
}
StartSender(mode) {
    if A_Args.Length >= 2 {
        Sleep(300) ; install lazy X11 grabs/raw subscriptions before injection
        FileAppend(mode "`n", A_Args[2])
        return
    }
    sender := A_ScriptDir "/trace_sender.ahk"
    Run('"' A_AhkPath '" "' sender '" ' mode)
}
global PriorCapsLock := GetKeyState("CapsLock", "T")
RestoreLockState(*) {
    global PriorCapsLock
    SetCapsLockState(PriorCapsLock ? "On" : "Off")
}
OnExit(RestoreLockState)
SetCapsLockState("Off")
Sleep(100)
Emit("meta", "runtime", 0, 0, 0, "v2:ptr" A_PtrSize)

global InputCase := "input-basic"
InputDown(_, vk, sc) => Emit(InputCase, "down", vk, sc)
InputUp(_, vk, sc) => Emit(InputCase, "up", vk, sc)
InputChar(_, chars) {
    Loop Parse chars
        Emit(InputCase, "char", 0, 0, Ord(A_LoopField))
}
ih := InputHook("I0 T10")
externalSender := A_Args.Length >= 2
ih.VisibleText := externalSender
ih.VisibleNonText := externalSender
ih.OnKeyDown := InputDown
ih.OnKeyUp := InputUp
ih.OnChar := InputChar
ih.KeyOpt("{All}", "N")
ih.Start()
StartSender("input")
Sleep(1200)
ih.Stop()
ih.Wait()
Emit(InputCase, "end", 0, 0, 0, ih.EndReason)

HotDown(*) => Emit("hotkeys", "fire", 0, 0, 0, A_ThisHotkey)
HotUp(*) => Emit("hotkeys", "fire", 0, 0, 0, A_ThisHotkey)
Hotkey("^F11", HotDown, "I0")
Hotkey("F12 Up", HotUp, "I0")
StartSender("hotkeys")
Sleep(1200)
Hotkey("^F11", "Off")
Hotkey("F12 Up", "Off")

HotstringHit(*) => Emit("hotstring", "fire", 0, 0, 0, A_ThisHotkey)
Hotstring(":B0*:zxq", HotstringHit)
StartSender("hotstring")
Sleep(1200)
Hotstring(":B0*:zxq", "Off")

WildHit(*) => Emit("wildcard", "fire", 0, 0, 0, A_ThisHotkey)
Hotkey("*F10", WildHit, "I0")
StartSender("wildcard")
Sleep(1200)
Hotkey("*F10", "Off")

CaseHotstring(*) => Emit("hotstring-case", "fire", 0, 0, 0, A_ThisHotkey)
Hotstring(":C*B0:zXc", CaseHotstring)
StartSender("case-hotstring")
Sleep(1200)
Hotstring(":C*B0:zXc", "Off")

InsideUnexpected(*) => Emit("hotstring-inside", "unexpected", 0, 0, 0, A_ThisHotkey)
InsideHit(*) => Emit("hotstring-inside", "fire", 0, 0, 0, A_ThisHotkey)
Hotstring(":B0*:nom", InsideUnexpected)
Hotstring(":?B0*:yes", InsideHit)
StartSender("inside-hotstring")
Sleep(1200)
Hotstring(":B0*:nom", "Off")
Hotstring(":?B0*:yes", "Off")

EndHotstring(*) => Emit("hotstring-end", "fire", 0, 0,
    StrLen(A_EndChar) ? Ord(A_EndChar) : 0, A_ThisHotkey)
Hotstring(":B0O:omx", EndHotstring)
StartSender("end-hotstring")
Sleep(1200)
Hotstring(":B0O:omx", "Off")
Emit("meta", "complete")
ExitApp
