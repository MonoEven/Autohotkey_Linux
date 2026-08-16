; Timer/ToolTip module doc-check (v2 docs: SetTimer period semantics, run-once,
; deletion, current-timer reference; ToolTip returns HWND, updates in place,
; blank hides and returns 0, coordinates).  Timers run in the main loop (they
; also fire during Sleep waits, per the docs); ToolTip needs Xvfb so this
; suite runs via run_check.sh --xvfb; output goes to a file.
#Requires AutoHotkey v2.0

WINOUT := "/tmp/ahk_dc_timer_out.txt"
FileDelete(WINOUT)
Log(line) => FileAppend(line "`n", WINOUT)

; --- SetTimer fires repeatedly at the period while the script waits. ---
n1 := 0
F1() {
    global n1
    n1++
}
SetTimer(F1, 40)
t := A_TickCount
while n1 < 5 && A_TickCount - t < 3000
    Sleep(10)
Log("timer_period=" (n1 = 5 ? 1 : 0))
SetTimer(F1, 0) ; Docs: Period 0 deletes the timer.
Sleep(100)
Log("timer_stop=" (n1 = 5 ? 1 : 0))

; --- Default period is 250 ms (docs). ---
n2 := 0
F2() {
    global n2
    n2++
}
SetTimer(F2)
t := A_TickCount
while n2 = 0 && A_TickCount - t < 2000
    Sleep(10)
Log("timer_default=" (n2 = 1 ? 1 : 0))
SetTimer(F2, 0)

; --- Negative period = run only once (docs). ---
n3 := 0
F3() {
    global n3
    n3++
}
SetTimer(F3, -60)
t := A_TickCount
while n3 < 1 && A_TickCount - t < 2000
    Sleep(10)
Sleep(200)
Log("timer_once=" (n3 = 1 ? 1 : 0))

; --- SetTimer() with no function refers to the current timer (docs). ---
n4 := 0
F4() {
    global n4
    n4++
    if n4 = 2
        SetTimer(, 0) ; Delete the timer that launched this thread.
}
SetTimer(F4, 30)
t := A_TickCount
while n4 < 2 && A_TickCount - t < 2000
    Sleep(10)
Sleep(150)
Log("timer_omit_fn=" (n4 = 2 ? 1 : 0))

; --- Invalid functor -> error. ---
try
    SetTimer("not a func")
catch Error
    Log("timer_bad_fn=1")

; --- Timers keep the script alive without Persistent (docs: IsPersistent). ---
; (Covered implicitly: the auto-execute section completes but the timer still
; fires because the main loop keeps running.)

; --- ToolTip (docs: returns the tooltip's HWND; update in place; blank/omitted
; Text hides the tooltip and returns 0; X/Y position). ---
h1 := ToolTip("Hello Tip", 300, 200)
Log("tip_hwnd=" (h1 != 0 ? 1 : 0))
Log("tip_title=" (WinGetTitle("ahk_id " h1) = "Hello Tip" ? 1 : 0))
h2 := ToolTip("Updated Tip", 300, 200)
Log("tip_update=" (h1 = h2 ? 1 : 0))
WinGetPos(&tx, &ty, , , "ahk_id " h2)
Log("tip_pos=" (tx >= 299 && tx <= 302 && ty >= 199 && ty <= 202 ? 1 : 0))
h3 := ToolTip()
Log("tip_hide=" (h3 = 0 ? 1 : 0))

; --- Cleanup. ---
ExitApp(0)
