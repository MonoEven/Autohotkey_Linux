; Control-module doc-check (v2 docs: Control* functions + WinGetControls/
; WinGetControlsHwnd).  Runs under Xvfb (run_check.sh --xvfb) with the
; xwin_helper test client that creates a main window plus child "control"
; windows and logs key/button events per window; output goes to a file.
#Requires AutoHotkey v2.0

WINOUT := "/tmp/ahk_dc_ctrl_out.txt"
FileDelete(WINOUT)
Log(line) => FileAppend(line "`n", WINOUT)

EVOUT := "/tmp/ahk_dc_ev.txt"
FileDelete(EVOUT)
prev_bytes := 0

Run('out/xwin_helper -title CtlMain -class DocCheck -x 100 -y 100 -w 400 -h 300 -evout ' EVOUT
    ' -child Edit1 Edit 30 30 120 24 -child Edit2 Edit 30 70 120 24'
    ' -child ButtonOK Button 200 40 90 30 -child ComboBox1 ComboBox 30 120 150 24'
    ' -child Hidden1 Hidden 10 10 20 20')
WinWait("CtlMain",, 5)
Sleep(300)

; Read the lines appended to the event file since the last call.
next_lines() {
    global prev_bytes
    f := FileOpen(EVOUT, "r")
    f.Seek(prev_bytes)
    rest := f.Read()
    f.Close()
    prev_bytes := FileGetSize(EVOUT)
    return StrSplit(rest, "`n")
}

; "name1,name2,..." of key-down events.
downs(lines) {
    out := ""
    for l in lines {
        if l = ""
            continue
        p := StrSplit(l, ":")
        if p.Length >= 3 && p[1] = "k" && p[2] = "down"
            out .= (out = "" ? "" : ",") p[3]
    }
    return out
}

; "down:N,up:N,..." of button events.
btns(lines) {
    out := ""
    for l in lines {
        if l = ""
            continue
        p := StrSplit(l, ":")
        if p.Length >= 3 && p[1] = "b"
            out .= (out = "" ? "" : ",") p[2] ":" p[3]
    }
    return out
}

; Distinct "hwnd1,hwnd2" of the windows that received events.
wins(lines) {
    out := ""
    for l in lines {
        if l = ""
            continue
        p := StrSplit(l, ":")
        if p.Length >= 5 && (p[1] = "k" || p[1] = "b")
            out .= (out = "" ? "" : ",") p[5]
    }
    ; Deduplicate preserving order.
    parts := StrSplit(out, ",")
    out := ""
    seen := Map()
    for p in parts {
        if p != "" && !seen.Has(p) {
            seen[p] := 1
            out .= (out = "" ? "" : ",") p
        }
    }
    return out
}

; --- WinGetControls / WinGetControlsHwnd (child enumeration + ClassNN). ---
ctrls := WinGetControls("CtlMain")
Log("ctrl_list=" (Join(ctrls) = "Edit1,Edit2,Button1,ComboBox1,Hidden1" ? 1 : 0))
hws := WinGetControlsHwnd("CtlMain")
Log("ctrl_hwnds=" (hws.Length = 5 ? 1 : 0))
Join(arr) {
    out := ""
    for v in arr
        out .= (out = "" ? "" : ",") v
    return out
}

; --- ControlGetText / ControlSetText (ClassNN, HWND and text identifiers). ---
Log("gettext_classnn=" (ControlGetText("Edit1", "CtlMain") = "Edit1" ? 1 : 0))
; check_detail0824 M0-B: GNOME desktop name and a leaked WAYLAND_DISPLAY must
; not override an explicit XDG_SESSION_TYPE=x11 session.
old_st := EnvGet("XDG_SESSION_TYPE")
old_desktop := EnvGet("XDG_CURRENT_DESKTOP")
old_wl := EnvGet("WAYLAND_DISPLAY")
EnvSet("XDG_SESSION_TYPE", "x11")
EnvSet("XDG_CURRENT_DESKTOP", "GNOME")
EnvSet("WAYLAND_DISPLAY", "wayland-leaked")
Log("gnome_x11_control=" (ControlGetText("Edit1", "CtlMain") = "Edit1" ? 1 : 0))
EnvSet("XDG_SESSION_TYPE", old_st)
EnvSet("XDG_CURRENT_DESKTOP", old_desktop)
EnvSet("WAYLAND_DISPLAY", old_wl)
ControlSetText("Hello World", "Edit1", "CtlMain")
Log("settext=" (ControlGetText("Edit1", "CtlMain") = "Hello World" ? 1 : 0))
ehwnd := ControlGetHwnd("Edit1", "CtlMain")
Log("gettext_hwnd=" (ControlGetText(ehwnd) = "Hello World" ? 1 : 0))
Log("gettext_ahkid=" (ControlGetText("ahk_id " ehwnd) = "Hello World" ? 1 : 0))
Log("gettext_textmatch=" (ControlGetText("Hello World", "CtlMain") = "Hello World" ? 1 : 0))
; TitleMatchMode 3 (exact): "Hello" must NOT match "Hello World".
SetTitleMatchMode(3)
try
    ControlGetText("Hello", "CtlMain")
catch TargetError
    Log("tm_exact_err=1")
SetTitleMatchMode(2)
; TitleMatchMode "RegEx".
SetTitleMatchMode("RegEx")
Log("tm_regex=" (ControlGetText("Hello W.*", "CtlMain") = "Hello World" ? 1 : 0))
SetTitleMatchMode(2)

; --- ControlGetPos / ControlMove. ---
ControlGetPos(&cx, &cy, &cw, &ch, "Button1", "CtlMain")
Log("getpos=" (cx = 200 && cy = 40 && cw = 90 && ch = 30 ? 1 : 0))
ControlMove(250, 50, , , "Button1", "CtlMain")
ControlGetPos(&cx, &cy, &cw, &ch, "Button1", "CtlMain")
Log("move_xy=" (cx = 250 && cy = 50 && cw = 90 && ch = 30 ? 1 : 0))
ControlMove(, , 110, 40, "Button1", "CtlMain")
ControlGetPos(&cx, &cy, &cw, &ch, "Button1", "CtlMain")
Log("move_wh=" (cx = 250 && cy = 50 && cw = 110 && ch = 40 ? 1 : 0))

; --- ControlGetHwnd / ControlGetClassNN. ---
bhwnd := ControlGetHwnd("Button1", "CtlMain")
Log("gethwnd_win=" (WinGetTitle("ahk_id " bhwnd) = "ButtonOK" ? 1 : 0))
Log("getclassnn_hwnd=" (ControlGetClassNN(bhwnd) = "Button1" ? 1 : 0))
Log("getclassnn_spec=" (ControlGetClassNN("Button1", "CtlMain") = "Button1" ? 1 : 0))

; --- ControlFocus / ControlGetFocus (returns HWND per docs). ---
ControlFocus("Edit2", "CtlMain")
focus_hwnd := ControlGetFocus("CtlMain")
Log("focus=" (focus_hwnd = ControlGetHwnd("Edit2", "CtlMain") ? 1 : 0))
Log("focus_classnn=" (ControlGetClassNN(focus_hwnd) = "Edit2" ? 1 : 0))

; --- ControlClick (ClassNN, position mode, buttons, count, D/U options). ---
ControlClick("Button1", "CtlMain")
Sleep(80)
cl_lines := next_lines()
Log("click=" (btns(cl_lines) = "down:1,up:1" ? 1 : 0))
Log("click_win=" (wins(cl_lines) = bhwnd ? 1 : 0))
ControlClick("Button1", "CtlMain", , "Right")
Sleep(80)
Log("click_right=" (btns(next_lines()) = "down:3,up:3" ? 1 : 0))
ControlClick("Button1", "CtlMain", , "Left", 2)
Sleep(100)
Log("click_count=" (btns(next_lines()) = "down:1,up:1,down:1,up:1" ? 1 : 0))
ControlClick("x350 y250", "CtlMain")
Sleep(80)
Log("click_xy=" (btns(next_lines()) = "down:1,up:1" ? 1 : 0))
main_hwnd := WinGetID("CtlMain")
; The position-mode click lands on the main window itself.
ControlClick("x350 y250", "CtlMain")
Sleep(80)
Log("click_xy_win=" (wins(next_lines()) = main_hwnd ? 1 : 0))
ControlClick("Button1", "CtlMain", , "Left", 1, "D")
Sleep(60)
Log("click_d=" (btns(next_lines()) = "down:1" ? 1 : 0))
ControlClick("Button1", "CtlMain", , "Left", 1, "U")
Sleep(60)
Log("click_u=" (btns(next_lines()) = "up:1" ? 1 : 0))

; --- ControlSend / ControlSendText (keystrokes reach the control). ---
ControlSend("ab", "Edit1", "CtlMain")
Sleep(80)
sd_lines := next_lines()
Log("send_keys=" (downs(sd_lines) = "a,b" ? 1 : 0))
Log("send_win=" (wins(sd_lines) = ehwnd ? 1 : 0))
ControlSendText("{x}", "Edit1", "CtlMain")
Sleep(80)
Log("sendtext=" (downs(next_lines()) = "Shift_L,braceleft,x,Shift_L,braceright" ? 1 : 0))

; --- M5-C A_ControlSendMode built-in variable (actual AT-SPI write is VM oracle). ---
Log("sendmode_default=" (A_ControlSendMode = "focus" ? 1 : 0))
A_ControlSendMode := "atspi"
Log("sendmode_set=" (A_ControlSendMode = "atspi" ? 1 : 0))
try {
    A_ControlSendMode := "bad"
    Log("sendmode_invalid=0")
} catch ValueError {
    Log("sendmode_invalid=1")
}
A_ControlSendMode := "focus"
Log("sendmode_restore=" (A_ControlSendMode = "focus" ? 1 : 0))

; --- M5-B: virtual state (style/exstyle/enabled/checked and Combo/List
; --- entries) has no real X11 effect on EXTERNAL windows; the port refuses
; --- to pretend success and throws OSError instead. ---
try ControlGetStyle("Button1", "CtlMain")
catch OSError
    Log("ns_style=1")
try ControlSetStyle("0x10", "Button1", "CtlMain")
catch OSError
    Log("ns_setstyle=1")
try ControlGetExStyle("Button1", "CtlMain")
catch OSError
    Log("ns_exstyle=1")
try ControlSetExStyle("^0x100", "Button1", "CtlMain")
catch OSError
    Log("ns_setexstyle=1")
try ControlGetEnabled("Button1", "CtlMain")
catch OSError
    Log("ns_enabled=1")
try ControlSetEnabled(0, "Button1", "CtlMain")
catch OSError
    Log("ns_setenabled=1")
try ControlGetChecked("Button1", "CtlMain")
catch OSError
    Log("ns_checked=1")
try ControlSetChecked(1, "Button1", "CtlMain")
catch OSError
    Log("ns_setchecked=1")
try ControlAddItem("A", "ComboBox1", "CtlMain")
catch OSError
    Log("ns_additem=1")
try ControlDeleteItem(1, "ComboBox1", "CtlMain")
catch OSError
    Log("ns_deleteitem=1")
try ControlFindItem("A", "ComboBox1", "CtlMain")
catch OSError
    Log("ns_finditem=1")
try ControlChooseIndex(1, "ComboBox1", "CtlMain")
catch OSError
    Log("ns_chooseindex=1")
try ControlChooseString("A", "ComboBox1", "CtlMain")
catch OSError
    Log("ns_choosestring=1")
try ControlGetChoice("ComboBox1", "CtlMain")
catch OSError
    Log("ns_getchoice=1")
try ControlGetIndex("ComboBox1", "CtlMain")
catch OSError
    Log("ns_getindex=1")
try ControlGetItems("ComboBox1", "CtlMain")
catch OSError
    Log("ns_getitems=1")
try ControlShowDropDown("ComboBox1", "CtlMain")
catch OSError
    Log("ns_showdd=1")
try ControlHideDropDown("ComboBox1", "CtlMain")
catch OSError
    Log("ns_hidedd=1")

; --- ControlGetVisible / ControlHide / ControlShow (REAL X11 operations). ---
Log("visible0=" (ControlGetVisible("Hidden1", "CtlMain") = 1 ? 1 : 0))
ControlHide("Hidden1", "CtlMain")
Log("hidden=" (ControlGetVisible("Hidden1", "CtlMain") = 0 ? 1 : 0))
ControlShow("Hidden1", "CtlMain")
Log("shown=" (ControlGetVisible("Hidden1", "CtlMain") = 1 ? 1 : 0))

; --- Error paths (docs: TargetError for window/control not found; the class
; --- gate still precedes the M5-B own-process gate). ---
try
    ControlGetText("Edit1", "NoSuchWindowPlease")
catch TargetError
    Log("err_window=1")
try
    ControlGetText("NoSuchControl", "CtlMain")
catch TargetError
    Log("err_control=1")
try
    ControlAddItem("x", "Edit1", "CtlMain")
catch TargetError
    Log("err_list_class=1")
try
    ControlGetFocus("NoSuchWindowPlease")
catch TargetError
    Log("err_focus=1")

; --- Cleanup. ---
Run("pkill -f xwin_helper")
