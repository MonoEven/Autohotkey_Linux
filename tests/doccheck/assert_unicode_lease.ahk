; Unicode borrow concurrency doc-check (check0820 P1):
; two INDEPENDENT AHK processes send different CJK characters to the SAME
; X server at (nearly) the same time.  The cross-process lease
; (AHK_UNICODE_BORROW_LEASE in core_input_linux.cpp) must serialize the
; borrows so neither mapping is clobbered.
; Requires --xvfb (xkeycap foreground client + XTEST via Send).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_lease_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

KCFILE := "/tmp/ahk_dc_keycap_lease.txt"
FileDelete(KCFILE)
Log("step[xkeycap-start]")
Run('out/xkeycap -out ' KCFILE ' -ms 120000')
WinWait("KeyCap Capture",, 5)
Log("step[winwait-rc]=" (WinExist("KeyCap Capture") ? 1 : 0))
Sleep(300)

; --- child script (a second AHK process on the same X server) ---
CHILD := "/tmp/ahk_lease_child.ahk"
childBody := "
(
#Requires AutoHotkey v2.0
SendText("世界世界世界世界")
FileAppend("done", "/tmp/ahk_lease_child_done.txt")
ExitApp 0
)"
FileDelete(CHILD)
FileAppend(childBody, CHILD)
Log("step[child-written]=" (FileExist(CHILD) ? 1 : 0))

; --- start the child, then send this process's text concurrently ---
childDone := "/tmp/ahk_lease_child_done.txt"
FileDelete(childDone)
Run(A_AhkPath ' "' CHILD '"')
Sleep(150)
Log("step[before-parent-send]=1")
SendText("你好你好你好")
Log("step[after-parent-send]=1")

; --- wait for the child (bounded; ASan builds serialize the two borrow
; windows at a fraction of native speed, so the wait must comfortably
; exceed the parent's own SendText) ---
i := 0
while !FileExist(childDone) && i < 24000 {
    Sleep(10)
    i += 1
}
Sleep(500)

; --- verify that all keysyms arrived at the foreground capture ---
kc := FileRead(KCFILE)
Log("lease_u4f60=" (InStr(kc, "U4F60") ? 1 : 0))
Log("lease_u597d=" (InStr(kc, "U597D") ? 1 : 0))
Log("lease_u4e16=" (InStr(kc, "U4E16") ? 1 : 0))
Log("lease_u754c=" (InStr(kc, "U754C") ? 1 : 0))
Log("lease_child_done=" (FileExist(childDone) ? 1 : 0))

; --- clean up the foreground client ---
RunWait("pkill -x xkeycap")
ExitApp 0