; OnMessage/SendMessage/PostMessage/MenuSelect doc-check (v2 docs).  Runs
; under Xvfb (run_check.sh --xvfb) with the xwin_helper test client.
;
; Linux semantics (documented in CHECK_REPORT):
;   - SendMessage/PostMessage validate MsgNumber (0..0xFFFFFFFF, ValueError
;     otherwise) and wParam/lParam (integer, or an object with a Ptr
;     property for SendMessage per docs), resolve the target window/control
;     per docs (TargetError if not found), then SendMessage returns 0 --
;     the reply a window gives for a message it does not handle
;     (DefWindowProc default); PostMessage returns nothing.  Timeout is
;     accepted but there is nothing to wait for.
;   - OnMessage registers/unregisters monitors with the exact upstream
;     semantics (functor validation incl. the 4-parameter limit, MaxThreads
;     1/-1/0); the callbacks can never fire because the port never receives
;     Win32 messages.
;   - MenuSelect resolves the target window (TargetError if not found) and
;     then raises the documented "does not have a standard Win32 menu"
;     TargetError, since no X11 window can have a standard Win32 menu.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_msg_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

Run('out/xwin_helper -title MsgMain -class DocCheck -x 100 -y 100 -w 400 -h 300'
    ' -child Edit1 Edit 30 30 120 24')
WinWait("MsgMain",, 5)
Sleep(300)

; --- SendMessage: returns 0 (the DefWindowProc default reply) ---
Log("sm_ctrl=" (SendMessage(0x1234, 0, 0, "Edit1", "MsgMain") = 0 ? 1 : 0))
Log("sm_win=" (SendMessage(0x1234, 5, 6, , "MsgMain") = 0 ? 1 : 0)) ; Control omitted: to the window.
Log("sm_lf=" (SendMessage(0x1234) = 0 ? 1 : 0)) ; Last Found Window (WinWait).
Log("sm_timeout=" (SendMessage(0x1234, 0, 0, "Edit1", "MsgMain", "", "", "", 50) = 0 ? 1 : 0))
buf := Buffer(4)
Log("sm_buffer=" (SendMessage(0x1234, buf, 0, "Edit1", "MsgMain") = 0 ? 1 : 0))

; ValueError: MsgNumber out of range (docs: 0..0xFFFFFFFF).
try {
    SendMessage(-1, 0, 0, "Edit1", "MsgMain")
    Log("sm_neg=0")
} catch ValueError {
    Log("sm_neg=1")
}
try {
    SendMessage(0x100000000, 0, 0, "Edit1", "MsgMain")
    Log("sm_big=0")
} catch ValueError {
    Log("sm_big=1")
}
; TargetError: window or control not found (docs).
try {
    SendMessage(0x1234, 0, 0, "Nope", "MsgMain")
    Log("sm_nope=0")
} catch TargetError {
    Log("sm_nope=1")
}
try {
    SendMessage(0x1234, 0, 0, "Edit1", "NoSuchWindow")
    Log("sm_nowin=0")
} catch TargetError {
    Log("sm_nowin=1")
}

; --- PostMessage: same validation, no return value ---
PostMessage(0x1234, 5, 6, "Edit1", "MsgMain")
Log("pm_ctrl=1")
PostMessage(0x1234)
Log("pm_lf=1")
; PostMessage: same validation; like SendMessage it accepts an object with a
; Ptr property (upstream passes Variant to PostSendMessage for both).
PostMessage(0x1234, buf, 0, "Edit1", "MsgMain")
Log("pm_obj=1")
try {
    PostMessage(-1, 0, 0, "Edit1", "MsgMain")
    Log("pm_neg=0")
} catch ValueError {
    Log("pm_neg=1")
}
try {
    PostMessage(0x1234, 0, 0, "Nope", "MsgMain")
    Log("pm_nope=0")
} catch TargetError {
    Log("pm_nope=1")
}

; --- OnMessage: register / re-register / unregister monitors ---
CB(wParam, lParam, msg, hwnd) => 0
OnMessage(0x1000, CB)
Log("om_reg=1")
OnMessage(0x1000, CB, 1) ; Option 2 (docs): registered after existing ones.
Log("om_again=1")
OnMessage(0x1000, CB, -1) ; Registered before existing ones (docs).
Log("om_priority=1")
OnMessage(0x1000, CB, 0) ; Unregister (docs).
Log("om_unreg=1")
OnMessage(0x1000, CB) ; Re-register.
Log("om_rereg=1")
OnMessage(0x1002, CB, 0) ; Deleting a non-existent monitor is a no-op.
Log("om_del_missing=1")

; TypeError: Callback must be a function object ("requires an Object").
try {
    OnMessage(0x1000, "notafunc")
    Log("om_str=0")
} catch Error {
    Log("om_str=1")
}
; Error: the callback accepts four parameters (docs) -- one with five
; required parameters is rejected.
F5(a, b, c, d, e) => 0
try {
    OnMessage(0x1000, F5)
    Log("om_5p=0")
} catch Error {
    Log("om_5p=1")
}
; ValueError: MsgNumber must be 0..0xFFFFFFFF.
try {
    OnMessage(-1, CB)
    Log("om_neg=0")
} catch ValueError {
    Log("om_neg=1")
}
try {
    OnMessage(0x100000000, CB)
    Log("om_big=0")
} catch ValueError {
    Log("om_big=1")
}

; --- MenuSelect: docs "A TargetError is thrown if the window or control
; could not be found, or does not have a standard Win32 menu." ---
try {
    MenuSelect("MsgMain", "", "File")
    Log("ms_nomenu=0")
} catch TargetError {
    Log("ms_nomenu=1")
}
try {
    MenuSelect("NoSuchWindow", "", "File")
    Log("ms_nowin=0")
} catch TargetError {
    Log("ms_nowin=1")
}

; --- Cleanup. ---
ExitApp(0)
