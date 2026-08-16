; NOT_IMPL error-behavior doc-check: every function that cannot be ported to
; Linux (GUI/COM/DllCall/hotstring/message/edit-control/selection machinery)
; must raise the clear "not been ported to Linux" runtime error instead of
; silently returning a wrong value.  Each entry is called with its minimum
; parameter count (dummy values) and the error message is verified.
; Runs headless: Log uses MsgBox, which prints to stdout (run_check.sh
; captures it).
#Requires AutoHotkey v2.0

; Headless MsgBox prints to stdout.
Log(line) => MsgBox(line)

Check(name, fn) {
    try {
        fn()
    } catch Error as e {
        if InStr(e.Message, "not been ported to Linux")
            Log("ni_" name "=1")
        else
            Log("ni_" name "=0 badmsg: " e.Message)
        return
    }
    Log("ni_" name "=0 noerror")
}

Check("GuiCtrlFromHwnd", () => GuiCtrlFromHwnd(1))
Check("GuiFromHwnd", () => GuiFromHwnd(1))
Check("Hotstring", () => Hotstring("::btw::by the way"))
Check("IL_Add", () => IL_Add(1, "x"))
Check("IL_Create", IL_Create)
Check("IL_Destroy", () => IL_Destroy(1))
Check("ImageSearch", () => ImageSearch(&ix, &iy, 0, 0, 10, 10, "x.png"))
Check("LoadPicture", () => LoadPicture("x"))
Check("MenuFromHandle", () => MenuFromHandle(1))
Check("MenuSelect", () => MenuSelect("x"))
Check("OnMessage", () => OnMessage(1, "x"))
Check("PostMessage", () => PostMessage(1, 0))
Check("RunAs", RunAs)
Check("SendMessage", () => SendMessage(1, 0, 0))
Check("WinSetRegion", WinSetRegion)

; --- Cleanup. ---
ExitApp(0)
